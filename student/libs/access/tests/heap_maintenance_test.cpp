#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/heap_maintenance.hpp"
#include "db/access/heap_page.hpp"
#include "db/access/heap_table.hpp"
#include "db/base/error.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/durability.hpp"
#include "db/storage/page.hpp"
#include "db/storage/replacement.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/xid.hpp"

namespace db::access {
namespace {

class MemoryDisk final : public storage::DiskManager {
public:
    auto read_page(const storage::PageAddress address) -> base::Result<storage::Page> override {
        const std::lock_guard lock{mutex_};
        const auto found = pages_.find(key(address));
        if (found == pages_.end()) {
            return base::fail(
                base::Error{base::ErrorCode::kNotFound, "heap maintenance page not found"}
            );
        }
        return storage::Page::load(found->second, address);
    }

    auto write_page(const storage::PageAddress address, const storage::Page& page)
        -> base::Status override {
        const std::lock_guard lock{mutex_};
        pages_[key(address)] = page.bytes();
        return {};
    }

    auto allocate_page(
        const base::RelationId relation_id,
        const storage::PageKind kind,
        const std::size_t special_size = 0
    ) -> base::Result<storage::PageAddress> override {
        const std::lock_guard lock{mutex_};
        auto& next = next_page_[relation_id.value()];
        const storage::PageAddress address{relation_id, base::PageId{next}};
        auto page = storage::Page::create(kind, address, special_size);
        if (!page) {
            return base::fail(std::move(page).error());
        }
        pages_[key(address)] = page->bytes();
        ++next;
        return address;
    }

    auto page_count(const base::RelationId relation_id) -> base::Result<std::uint64_t> override {
        const std::lock_guard lock{mutex_};
        const auto found = next_page_.find(relation_id.value());
        return found == next_page_.end() ? 0U : found->second;
    }

    auto sync_relation(base::RelationId relation_id) -> base::Status override {
        static_cast<void>(relation_id);
        return {};
    }

    auto sync_all() -> base::Status override {
        return {};
    }

    auto remove_relation(const base::RelationId relation_id, const bool missing_ok)
        -> base::Status override {
        const std::lock_guard lock{mutex_};
        const auto removed = next_page_.erase(relation_id.value());
        for (auto iterator = pages_.begin(); iterator != pages_.end();) {
            if (iterator->first.first == relation_id.value()) {
                iterator = pages_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        if (removed == 0U && !missing_ok) {
            return base::fail(
                base::Error{base::ErrorCode::kNotFound, "heap maintenance relation not found"}
            );
        }
        return {};
    }

private:
    using PageKey = std::pair<std::uint32_t, std::uint32_t>;

    static auto key(const storage::PageAddress address) noexcept -> PageKey {
        return {address.relation_id.value(), address.page_id.value()};
    }

    std::mutex mutex_;
    std::map<PageKey, storage::Page::Storage> pages_;
    std::map<std::uint32_t, std::uint32_t> next_page_;
};

class AcceptDurability final : public storage::PageDurabilityPort {
public:
    auto flush_through(const base::Lsn target_lsn) -> base::Status override {
        if (target_lsn == base::kInvalidLsn) {
            return base::fail(
                base::Error{base::ErrorCode::kInvalidArgument, "invalid durability target"}
            );
        }
        return {};
    }
};

class RecordingHeapLog final : public HeapMutationLogPort {
public:
    void clear() {
        operations_.clear();
    }

    void fail_next_prune() noexcept {
        fail_prune_ = true;
    }

    auto operations() const noexcept -> const std::vector<HeapWalOperation>& {
        return operations_;
    }

    auto log(const HeapWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.after_page == nullptr) {
            return base::fail(
                base::Error{base::ErrorCode::kInvalidArgument, "missing heap after-image"}
            );
        }
        operations_.push_back(mutation.operation);
        if (fail_prune_ && mutation.operation == HeapWalOperation::kPrune) {
            fail_prune_ = false;
            return base::fail(base::Error{base::ErrorCode::kIo, "injected heap prune WAL failure"});
        }
        const auto result = base::Lsn{next_lsn_};
        next_lsn_ += 8U;
        return result;
    }

private:
    std::vector<HeapWalOperation> operations_;
    bool fail_prune_{false};
    std::uint64_t next_lsn_{8};
};

class ConcurrentHeapLog final : public HeapMutationLogPort {
public:
    auto log(const HeapWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.after_page == nullptr) {
            return base::fail(
                base::Error{base::ErrorCode::kInvalidArgument, "missing heap after-image"}
            );
        }
        return base::Lsn{next_lsn_.fetch_add(8U, std::memory_order_relaxed)};
    }

private:
    std::atomic<std::uint64_t> next_lsn_{8};
};

auto make_pool() -> std::unique_ptr<storage::BufferPool> {
    auto pool = storage::BufferPool::create(
        8,
        std::make_unique<MemoryDisk>(),
        std::move(storage::LruReplacer::create(8)).value(),
        std::make_unique<AcceptDurability>()
    );
    EXPECT_TRUE(pool) << pool.error().to_string();
    return std::move(pool).value();
}

auto bytes(const std::string& value) -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.reserve(value.size());
    for (const auto character : value) {
        result.push_back(static_cast<std::byte>(character));
    }
    return result;
}

auto text(const std::vector<std::byte>& payload) -> std::string {
    std::string result;
    result.reserve(payload.size());
    for (const auto value : payload) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return result;
}

void set_status(
    transaction::CommitLog& commit_log,
    const base::Xid xid,
    const transaction::TransactionStatus status
) {
    ASSERT_TRUE(commit_log.ensure_xid_allocated(xid));
    ASSERT_TRUE(commit_log.set_status(xid, status));
}

auto mutation_snapshot(const base::Xid own_xid, const std::uint32_t command_id = 1)
    -> transaction::Snapshot {
    return transaction::Snapshot{
        .xmin = transaction::kFirstNormalXid,
        .xmax = base::Xid{own_xid.value() + 1U},
        .in_progress = {own_xid},
        .command_id = command_id,
    };
}

auto mutation_context(
    const base::Xid own_xid,
    const transaction::Snapshot& captured,
    const transaction::CommitLog& commit_log
) noexcept -> HeapMutationContext {
    return HeapMutationContext{
        .mvcc =
            HeapMvccContext{
                .own_xid = own_xid,
                .snapshot = &captured,
                .commit_log = &commit_log,
            },
        .command_id = captured.command_id,
    };
}

auto fetch_context(
    const base::Xid own_xid,
    const transaction::Snapshot& captured,
    const transaction::CommitLog& commit_log,
    const std::optional<base::Xid> prune_xmin = std::nullopt
) noexcept -> HeapFetchContext {
    return HeapFetchContext{
        .mvcc =
            HeapMvccContext{
                .own_xid = own_xid,
                .snapshot = &captured,
                .commit_log = &commit_log,
            },
        .prune_xmin = prune_xmin,
    };
}

auto pointer(storage::BufferPool& pool, const base::RelationId relation_id, const base::Rid rid)
    -> LinePointer {
    const storage::PageAddress address{relation_id, rid.page_id()};
    auto read = pool.read_page(address);
    EXPECT_TRUE(read);
    auto page_copy = *read->page();
    auto page = HeapPage::open(page_copy, address);
    EXPECT_TRUE(page);
    return page->line_pointer(rid.slot_id()).value();
}

auto page_image(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const base::PageId page_id
) -> storage::Page::Storage {
    auto read = pool.read_page(storage::PageAddress{relation_id, page_id});
    EXPECT_TRUE(read);
    return read->page()->bytes();
}

constexpr std::size_t kConcurrentHotWriterCount = 4;
constexpr std::size_t kConcurrentHotReaderCount = 8;
constexpr std::size_t kConcurrentHotUpdatesPerWriter = 100;
constexpr std::size_t kConcurrentHotReadsPerReader = 2'000;
constexpr std::size_t kConcurrentHotUpdateAttempts = 10'000;
constexpr std::uint32_t kConcurrentHotFirstWriterXid = 20;
constexpr std::uint32_t kConcurrentHotFirstReaderXid = 5'000'000;
constexpr std::uint32_t kConcurrentHotSnapshotXmax = 6'000'000;
static_assert(
    kConcurrentHotFirstWriterXid
        + kConcurrentHotWriterCount * kConcurrentHotUpdatesPerWriter * kConcurrentHotUpdateAttempts
    < kConcurrentHotFirstReaderXid
);

struct ConcurrentHotStress final {
    HeapTable& table;
    transaction::CommitLog& commit_log;
    base::Rid root;
    const std::vector<std::byte>& payload;
    std::atomic<std::uint32_t> next_xid{kConcurrentHotFirstWriterXid};
    std::atomic<std::size_t> failures{0};
    std::atomic<std::size_t> updates{0};
    std::atomic<std::size_t> vacuum_errors{0};
    std::atomic<std::size_t> update_errors{0};
    std::atomic<std::size_t> cold_updates{0};
    std::atomic<std::size_t> status_errors{0};
    std::atomic<std::size_t> retry_exhaustions{0};
    std::atomic<std::size_t> stale_target_retries{0};
    std::atomic<std::size_t> reader_misses{0};
    std::atomic<bool> start{false};
    std::mutex active_writer_mutex{};
    std::vector<base::Xid> active_writer_xids{};
    std::mutex first_error_mutex{};
    std::string first_update_error{};
};

auto concurrent_hot_snapshot(const base::Xid xid) -> transaction::Snapshot {
    return transaction::Snapshot{
        .xmin = transaction::kFirstNormalXid,
        .xmax = base::Xid{kConcurrentHotSnapshotXmax},
        .in_progress = {xid},
        .command_id = 1,
    };
}

auto begin_concurrent_hot_attempt(ConcurrentHotStress& stress, const base::Xid xid)
    -> base::Result<transaction::Snapshot> {
    auto allocated = stress.commit_log.ensure_xid_allocated(xid);
    if (!allocated) {
        return base::fail(std::move(allocated).error());
    }
    const std::lock_guard lock{stress.active_writer_mutex};
    stress.active_writer_xids.push_back(xid);
    auto in_progress = stress.active_writer_xids;
    std::ranges::sort(in_progress, {}, [](const base::Xid value) {
        return value.value();
    });
    const auto xmin = in_progress.at(0U);
    return transaction::Snapshot{
        .xmin = xmin,
        .xmax = base::Xid{stress.next_xid.load(std::memory_order_relaxed)},
        .in_progress = std::move(in_progress),
        .command_id = 1,
    };
}

void end_concurrent_hot_attempt(ConcurrentHotStress& stress, const base::Xid xid) {
    const std::lock_guard lock{stress.active_writer_mutex};
    std::erase(stress.active_writer_xids, xid);
}

auto concurrent_hot_oldest_xmin(ConcurrentHotStress& stress) -> base::Xid {
    const std::lock_guard lock{stress.active_writer_mutex};
    if (stress.active_writer_xids.empty()) {
        return base::Xid{stress.next_xid.load(std::memory_order_relaxed)};
    }
    return *std::ranges::min_element(stress.active_writer_xids, {}, [](const base::Xid value) {
        return value.value();
    });
}

void wait_for_concurrent_hot_start(const ConcurrentHotStress& stress) noexcept {
    while (!stress.start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void record_concurrent_hot_failure(ConcurrentHotStress& stress) noexcept {
    stress.failures.fetch_add(1U, std::memory_order_relaxed);
}

enum class ConcurrentHotUpdateResult : std::uint8_t {
    kRetry,
    kSucceeded,
    kFailed,
};

auto try_concurrent_hot_update(
    ConcurrentHotStress& stress,
    const base::Xid xid,
    const transaction::Snapshot& captured
) -> ConcurrentHotUpdateResult {
    auto visible = stress.table.fetch(
        stress.root,
        fetch_context(xid, captured, stress.commit_log, transaction::kXidWrapBoundary)
    );
    if (!visible || !visible->has_value()) {
        return ConcurrentHotUpdateResult::kRetry;
    }
    auto changed = stress.table.update(
        HeapUpdateRequest{
            .target = visible->value().rid,
            .payload = stress.payload,
            .indexed_columns_unchanged = true,
        },
        mutation_context(xid, captured, stress.commit_log)
    );
    if (!changed) {
        if (changed.error().code() == base::ErrorCode::kNotFound
            || changed.error().code() == base::ErrorCode::kOutOfRange) {
            stress.stale_target_retries.fetch_add(1U, std::memory_order_relaxed);
            return ConcurrentHotUpdateResult::kRetry;
        }
        stress.update_errors.fetch_add(1U, std::memory_order_relaxed);
        const std::lock_guard lock{stress.first_error_mutex};
        if (stress.first_update_error.empty()) {
            stress.first_update_error = changed.error().to_string();
        }
        return ConcurrentHotUpdateResult::kFailed;
    }
    const auto* success = std::get_if<HeapUpdateSuccess>(&changed.value());
    if (success == nullptr) {
        return ConcurrentHotUpdateResult::kRetry;
    }
    if (success->kind != HeapUpdateKind::kHot) {
        stress.cold_updates.fetch_add(1U, std::memory_order_relaxed);
        return ConcurrentHotUpdateResult::kFailed;
    }
    if (!stress.commit_log.set_status(xid, transaction::TransactionStatus::kCommitted)) {
        stress.status_errors.fetch_add(1U, std::memory_order_relaxed);
        return ConcurrentHotUpdateResult::kFailed;
    }
    return ConcurrentHotUpdateResult::kSucceeded;
}

auto run_concurrent_hot_update(ConcurrentHotStress& stress) -> bool {
    for (std::size_t attempt = 0; attempt < kConcurrentHotUpdateAttempts; ++attempt) {
        const auto xid = base::Xid{stress.next_xid.fetch_add(1U, std::memory_order_relaxed)};
        auto captured = begin_concurrent_hot_attempt(stress, xid);
        if (!captured) {
            return false;
        }
        const auto result = try_concurrent_hot_update(stress, xid, captured.value());
        if (result == ConcurrentHotUpdateResult::kSucceeded) {
            end_concurrent_hot_attempt(stress, xid);
            return true;
        }
        const auto aborted =
            stress.commit_log.set_status(xid, transaction::TransactionStatus::kAborted);
        end_concurrent_hot_attempt(stress, xid);
        if (!aborted) {
            return false;
        }
        if (result == ConcurrentHotUpdateResult::kFailed) {
            return false;
        }
        std::this_thread::yield();
    }
    stress.retry_exhaustions.fetch_add(1U, std::memory_order_relaxed);
    return false;
}

auto vacuum_concurrent_hot_relation(ConcurrentHotStress& stress) -> bool {
    for (;;) {
        auto vacuumed = stress.table.vacuum(stress.commit_log, concurrent_hot_oldest_xmin(stress));
        if (!vacuumed) {
            stress.vacuum_errors.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        if (vacuumed->pages_skipped_busy == 0U) {
            return true;
        }
        std::this_thread::yield();
    }
}

void run_concurrent_hot_writer(ConcurrentHotStress& stress) {
    wait_for_concurrent_hot_start(stress);
    for (std::size_t update = 0; update < kConcurrentHotUpdatesPerWriter; ++update) {
        if (!vacuum_concurrent_hot_relation(stress) || !run_concurrent_hot_update(stress)
            || !vacuum_concurrent_hot_relation(stress)) {
            record_concurrent_hot_failure(stress);
            return;
        }
        stress.updates.fetch_add(1U, std::memory_order_relaxed);
    }
}

void run_concurrent_hot_reader(ConcurrentHotStress& stress, const std::size_t reader) {
    const auto xid = base::Xid{kConcurrentHotFirstReaderXid + static_cast<std::uint32_t>(reader)};
    const auto captured = concurrent_hot_snapshot(xid);
    wait_for_concurrent_hot_start(stress);
    for (std::size_t read = 0; read < kConcurrentHotReadsPerReader; ++read) {
        auto visible = stress.table.fetch(
            stress.root,
            fetch_context(xid, captured, stress.commit_log, transaction::kXidWrapBoundary)
        );
        if (!visible || !visible->has_value() || visible->value().payload != stress.payload) {
            stress.reader_misses.fetch_add(1U, std::memory_order_relaxed);
            record_concurrent_hot_failure(stress);
            return;
        }
        std::this_thread::yield();
    }
}

void join_concurrent_hot_workers(std::vector<std::thread>& workers) {
    for (auto& worker : workers) {
        worker.join();
    }
}

TEST(HeapMaintenanceTest, DeadTidStoreDeduplicatesAndReturnsStableOrder) {
    DeadTidStore store;
    const auto first = base::Rid{base::PageId{2}, base::SlotId{3}};
    const auto second = base::Rid{base::PageId{1}, base::SlotId{7}};

    EXPECT_TRUE(store.empty());
    EXPECT_FALSE(store.insert(base::kInvalidRid));
    EXPECT_TRUE(store.insert(first));
    EXPECT_TRUE(store.insert(second));
    EXPECT_FALSE(store.insert(first));
    EXPECT_EQ(store.size(), 2U);
    EXPECT_TRUE(store.contains(first));
    EXPECT_FALSE(store.contains(base::Rid{base::PageId{9}, base::SlotId{9}}));

    const auto tids = store.tids();
    ASSERT_EQ(tids.size(), 2U);
    EXPECT_EQ(tids[0], second);
    EXPECT_EQ(tids[1], first);
}

TEST(HeapMaintenanceTest, VacuumCollapsesHotAndReclaimsAbortedVersionsSafely) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{401};
    auto table = HeapTable::create(relation_id, *pool, registry, log, 100).value();
    transaction::CommitLog commit_log;

    const auto hot_root = table.insert(base::Xid{10}, 0, bytes("v1")).value();
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    const auto hot_snapshot = mutation_snapshot(base::Xid{20});
    auto hot = table.update(
        HeapUpdateRequest{
            .target = hot_root,
            .payload = bytes("v2"),
            .indexed_columns_unchanged = true,
        },
        mutation_context(base::Xid{20}, hot_snapshot, commit_log)
    );
    ASSERT_TRUE(hot);
    const auto hot_successor = std::get<HeapUpdateSuccess>(hot.value()).rid;
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);

    const auto deleted = table.insert(base::Xid{11}, 0, bytes("delete")).value();
    set_status(commit_log, base::Xid{11}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{21}));
    const auto delete_snapshot = mutation_snapshot(base::Xid{21});
    ASSERT_TRUE(table.delete_tuple(
        HeapDeleteRequest{.target = deleted},
        mutation_context(base::Xid{21}, delete_snapshot, commit_log)
    ));
    set_status(commit_log, base::Xid{21}, transaction::TransactionStatus::kCommitted);

    const auto aborted_insert = table.insert(base::Xid{30}, 0, bytes("aborted insert")).value();
    set_status(commit_log, base::Xid{30}, transaction::TransactionStatus::kAborted);
    const auto aborted_hot_root = table.insert(base::Xid{12}, 0, bytes("kept")).value();
    set_status(commit_log, base::Xid{12}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{31}));
    const auto aborted_hot_snapshot = mutation_snapshot(base::Xid{31});
    auto aborted_hot = table.update(
        HeapUpdateRequest{
            .target = aborted_hot_root,
            .payload = bytes("never visible"),
            .indexed_columns_unchanged = true,
        },
        mutation_context(base::Xid{31}, aborted_hot_snapshot, commit_log)
    );
    ASSERT_TRUE(aborted_hot);
    const auto aborted_hot_successor = std::get<HeapUpdateSuccess>(aborted_hot.value()).rid;
    set_status(commit_log, base::Xid{31}, transaction::TransactionStatus::kAborted);
    log.clear();

    auto vacuumed = table.vacuum(commit_log, transaction::kXidWrapBoundary);

    ASSERT_TRUE(vacuumed);
    EXPECT_EQ(vacuumed->pages_scanned, 1U);
    EXPECT_EQ(vacuumed->pages_skipped_busy, 0U);
    EXPECT_EQ(vacuumed->redirects_installed, 1U);
    EXPECT_EQ(vacuumed->tuples_pruned, 4U);
    EXPECT_GT(vacuumed->space_reclaimed, 0U);
    EXPECT_EQ(vacuumed->line_pointers_truncated, 1U);
    const auto dead_tids = vacuumed->dead_tids.tids();
    ASSERT_EQ(dead_tids.size(), 2U);
    EXPECT_EQ(dead_tids[0], deleted);
    EXPECT_EQ(dead_tids[1], aborted_insert);
    EXPECT_TRUE(vacuumed->dead_tids.contains(deleted));
    EXPECT_FALSE(vacuumed->dead_tids.contains(aborted_hot_successor));
    EXPECT_EQ(pointer(*pool, relation_id, hot_root).state, LinePointerState::kRedirect);
    EXPECT_EQ(pointer(*pool, relation_id, deleted).state, LinePointerState::kDead);
    EXPECT_EQ(pointer(*pool, relation_id, aborted_insert).state, LinePointerState::kDead);
    EXPECT_EQ(pointer(*pool, relation_id, aborted_hot_root).state, LinePointerState::kNormal);
    EXPECT_EQ(log.operations(), (std::vector{HeapWalOperation::kPrune}));

    const auto reader_snapshot = transaction::Snapshot{
        .xmin = transaction::kFirstNormalXid,
        .xmax = base::Xid{100},
        .in_progress = {base::Xid{90}},
        .command_id = 1,
    };
    auto current = table.fetch(hot_root, fetch_context(base::Xid{90}, reader_snapshot, commit_log));
    ASSERT_TRUE(current);
    ASSERT_TRUE(current->has_value());
    EXPECT_EQ(current->value().rid, hot_successor);
    EXPECT_EQ(text(current->value().payload), "v2");
    auto kept =
        table.fetch(aborted_hot_root, fetch_context(base::Xid{90}, reader_snapshot, commit_log));
    ASSERT_TRUE(kept);
    ASSERT_TRUE(kept->has_value());
    EXPECT_EQ(text(kept->value().payload), "kept");

    const auto before_cleanup = table.insert(base::Xid{40}, 0, bytes("not a reused dead RID"));
    ASSERT_TRUE(before_cleanup);
    EXPECT_NE(before_cleanup.value(), deleted);
    EXPECT_NE(before_cleanup.value(), aborted_insert);
    auto reclaimed = table.reclaim_index_cleaned_tids(vacuumed->dead_tids);
    ASSERT_TRUE(reclaimed) << reclaimed.error().to_string();
    EXPECT_EQ(reclaimed->pages_scanned, 1U);
    EXPECT_EQ(reclaimed->pages_skipped_busy, 0U);
    EXPECT_EQ(reclaimed->tids_reclaimed, 2U);
    EXPECT_EQ(pointer(*pool, relation_id, deleted).state, LinePointerState::kUnused);
    EXPECT_EQ(pointer(*pool, relation_id, aborted_insert).state, LinePointerState::kUnused);
    const auto after_cleanup = table.insert(base::Xid{41}, 0, bytes("reuses cleaned RID"));
    ASSERT_TRUE(after_cleanup);
    EXPECT_EQ(after_cleanup.value(), deleted);
}

TEST(HeapMaintenanceTest, OpportunisticPruneSkipsBusyPageThenPublishesReclaimedSpace) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{402};
    auto table = HeapTable::create(relation_id, *pool, registry, log, 100).value();
    transaction::CommitLog commit_log;
    const std::vector<std::byte> payload(1'800, std::byte{0x41});
    const auto root = table.insert(base::Xid{10}, 0, payload).value();
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    const auto update_snapshot = mutation_snapshot(base::Xid{20});
    auto updated = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = payload,
            .indexed_columns_unchanged = true,
        },
        mutation_context(base::Xid{20}, update_snapshot, commit_log)
    );
    ASSERT_TRUE(updated);
    ASSERT_EQ(std::get<HeapUpdateSuccess>(updated.value()).kind, HeapUpdateKind::kHot);
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    log.clear();

    std::optional<storage::PinGuard> competing{
        std::move(pool->pin_page(storage::PageAddress{relation_id, root.page_id()})).value(),
    };
    auto busy_vacuum = table.vacuum(commit_log, transaction::kXidWrapBoundary);
    ASSERT_TRUE(busy_vacuum);
    EXPECT_EQ(busy_vacuum->pages_skipped_busy, 1U);
    EXPECT_EQ(pointer(*pool, relation_id, root).state, LinePointerState::kNormal);
    EXPECT_TRUE(log.operations().empty());

    const auto reader_snapshot = transaction::Snapshot{
        .xmin = transaction::kFirstNormalXid,
        .xmax = base::Xid{40},
        .in_progress = {base::Xid{30}},
        .command_id = 1,
    };
    auto busy_fetch = table.fetch(
        root,
        fetch_context(base::Xid{30}, reader_snapshot, commit_log, transaction::kXidWrapBoundary)
    );
    ASSERT_TRUE(busy_fetch);
    ASSERT_TRUE(busy_fetch->has_value());
    EXPECT_EQ(pointer(*pool, relation_id, root).state, LinePointerState::kNormal);

    competing.reset();
    auto pruned_fetch = table.fetch(
        root,
        fetch_context(base::Xid{30}, reader_snapshot, commit_log, transaction::kXidWrapBoundary)
    );
    ASSERT_TRUE(pruned_fetch);
    ASSERT_TRUE(pruned_fetch->has_value());
    EXPECT_EQ(pointer(*pool, relation_id, root).state, LinePointerState::kRedirect);
    EXPECT_EQ(log.operations(), (std::vector{HeapWalOperation::kPrune}));

    auto reusable = table.find_page_with_space(
        static_cast<std::uint16_t>(kTupleHeaderSize + payload.size()),
        HeapSpacePolicy::kHotUpdate
    );
    ASSERT_TRUE(reusable);
    EXPECT_EQ(reusable.value(), std::optional<base::PageId>{root.page_id()});
}

TEST(HeapMaintenanceTest, IndexCleanedTidReclaimSkipsBusyPageAndRetries) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{409};
    auto table = HeapTable::create(relation_id, *pool, registry, log, 100).value();
    transaction::CommitLog commit_log;
    const auto aborted = table.insert(base::Xid{10}, 0, bytes("aborted")).value();
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kAborted);
    auto vacuumed = table.vacuum(commit_log, transaction::kXidWrapBoundary);
    ASSERT_TRUE(vacuumed) << vacuumed.error().to_string();
    ASSERT_TRUE(vacuumed->dead_tids.contains(aborted));

    {
        auto competing_pin = pool->pin_page(storage::PageAddress{relation_id, aborted.page_id()});
        ASSERT_TRUE(competing_pin) << competing_pin.error().to_string();
        auto skipped = table.reclaim_index_cleaned_tids(vacuumed->dead_tids);
        ASSERT_TRUE(skipped) << skipped.error().to_string();
        EXPECT_EQ(skipped->pages_scanned, 1U);
        EXPECT_EQ(skipped->pages_skipped_busy, 1U);
        EXPECT_EQ(skipped->tids_reclaimed, 0U);
        EXPECT_EQ(pointer(*pool, relation_id, aborted).state, LinePointerState::kDead);
    }

    log.fail_next_prune();
    auto failed = table.reclaim_index_cleaned_tids(vacuumed->dead_tids);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(pointer(*pool, relation_id, aborted).state, LinePointerState::kDead);

    auto retried = table.reclaim_index_cleaned_tids(vacuumed->dead_tids);
    ASSERT_TRUE(retried) << retried.error().to_string();
    EXPECT_EQ(retried->pages_skipped_busy, 0U);
    EXPECT_EQ(retried->tids_reclaimed, 1U);
    EXPECT_EQ(retried->line_pointers_truncated, 1U);
    auto reused = table.insert(base::Xid{11}, 0, bytes("reused after index cleanup"));
    ASSERT_TRUE(reused) << reused.error().to_string();
    EXPECT_EQ(reused.value(), aborted);
}

TEST(HeapMaintenanceTest, OldSnapshotRemainsVisibleDuringOpportunisticPrune) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{406};
    auto table = HeapTable::create(relation_id, *pool, registry, log, 100).value();
    transaction::CommitLog commit_log;
    const std::vector<std::byte> old_payload(1'800, std::byte{0x31});
    const std::vector<std::byte> new_payload(1'800, std::byte{0x32});
    const auto root = table.insert(base::Xid{10}, 0, old_payload).value();
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    const auto update_snapshot = mutation_snapshot(base::Xid{20});
    ASSERT_TRUE(table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = new_payload,
            .indexed_columns_unchanged = true,
        },
        mutation_context(base::Xid{20}, update_snapshot, commit_log)
    ));
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    log.clear();

    const auto old_snapshot = transaction::Snapshot{
        .xmin = base::Xid{10},
        .xmax = base::Xid{20},
        .in_progress = {},
        .command_id = 1,
    };
    auto old_version =
        table.fetch(root, fetch_context(base::Xid{30}, old_snapshot, commit_log, base::Xid{20}));

    ASSERT_TRUE(old_version);
    ASSERT_TRUE(old_version->has_value());
    EXPECT_EQ(old_version->value().rid, root);
    EXPECT_EQ(old_version->value().payload, old_payload);
    EXPECT_EQ(pointer(*pool, relation_id, root).state, LinePointerState::kNormal);
    EXPECT_TRUE(log.operations().empty());

    const auto new_snapshot = transaction::Snapshot{
        .xmin = base::Xid{21},
        .xmax = base::Xid{40},
        .in_progress = {base::Xid{30}},
        .command_id = 1,
    };
    auto new_version =
        table.fetch(root, fetch_context(base::Xid{30}, new_snapshot, commit_log, base::Xid{21}));
    ASSERT_TRUE(new_version);
    ASSERT_TRUE(new_version->has_value());
    EXPECT_EQ(new_version->value().payload, new_payload);
    EXPECT_EQ(pointer(*pool, relation_id, root).state, LinePointerState::kRedirect);
    EXPECT_EQ(log.operations(), (std::vector{HeapWalOperation::kPrune}));
}

TEST(HeapMaintenanceTest, VacuumMarksWholeHotChainDeadAfterColdTerminalUpdate) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{407};
    auto table = HeapTable::create(relation_id, *pool, registry, log, 100).value();
    transaction::CommitLog commit_log;
    const auto root = table.insert(base::Xid{10}, 0, bytes("v1")).value();
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    const auto hot_snapshot = mutation_snapshot(base::Xid{20});
    auto hot = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("v2"),
            .indexed_columns_unchanged = true
        },
        mutation_context(base::Xid{20}, hot_snapshot, commit_log)
    );
    ASSERT_TRUE(hot);
    const auto hot_successor = std::get<HeapUpdateSuccess>(hot.value()).rid;
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);

    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{30}));
    const auto cold_snapshot = mutation_snapshot(base::Xid{30}, 2);
    auto cold = table.update(
        HeapUpdateRequest{
            .target = hot_successor,
            .payload = bytes("v3"),
            .indexed_columns_unchanged = false,
        },
        mutation_context(base::Xid{30}, cold_snapshot, commit_log)
    );
    ASSERT_TRUE(cold);
    const auto cold_success = std::get<HeapUpdateSuccess>(cold.value());
    ASSERT_EQ(cold_success.kind, HeapUpdateKind::kCold);
    set_status(commit_log, base::Xid{30}, transaction::TransactionStatus::kCommitted);
    log.clear();

    auto vacuumed = table.vacuum(commit_log, transaction::kXidWrapBoundary);

    ASSERT_TRUE(vacuumed);
    EXPECT_EQ(vacuumed->redirects_installed, 0U);
    EXPECT_TRUE(vacuumed->dead_tids.contains(root));
    EXPECT_EQ(pointer(*pool, relation_id, root).state, LinePointerState::kDead);
    EXPECT_EQ(pointer(*pool, relation_id, hot_successor).state, LinePointerState::kUnused);
    EXPECT_EQ(pointer(*pool, relation_id, cold_success.rid).state, LinePointerState::kNormal);

    const auto reader_snapshot = transaction::Snapshot{
        .xmin = base::Xid{31},
        .xmax = base::Xid{50},
        .in_progress = {base::Xid{40}},
        .command_id = 1,
    };
    auto current =
        table.fetch(cold_success.rid, fetch_context(base::Xid{40}, reader_snapshot, commit_log));
    ASSERT_TRUE(current);
    ASSERT_TRUE(current->has_value());
    EXPECT_EQ(text(current->value().payload), "v3");
}

TEST(HeapMaintenanceTest, RepeatedVacuumKeepsOneRedirectRoot) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{403};
    auto table = HeapTable::create(relation_id, *pool, registry, log, 100).value();
    transaction::CommitLog commit_log;
    const auto root = table.insert(base::Xid{10}, 0, bytes("v1")).value();
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    const auto first_snapshot = mutation_snapshot(base::Xid{20});
    auto first = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("v2"),
            .indexed_columns_unchanged = true
        },
        mutation_context(base::Xid{20}, first_snapshot, commit_log)
    );
    ASSERT_TRUE(first);
    const auto first_successor = std::get<HeapUpdateSuccess>(first.value()).rid;
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(table.vacuum(commit_log, transaction::kXidWrapBoundary));

    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{30}));
    const auto second_snapshot = mutation_snapshot(base::Xid{30}, 2);
    auto second = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("v3"),
            .indexed_columns_unchanged = true
        },
        mutation_context(base::Xid{30}, second_snapshot, commit_log)
    );
    ASSERT_TRUE(second);
    const auto second_successor = std::get<HeapUpdateSuccess>(second.value()).rid;
    set_status(commit_log, base::Xid{30}, transaction::TransactionStatus::kCommitted);

    auto second_vacuum = table.vacuum(commit_log, transaction::kXidWrapBoundary);

    ASSERT_TRUE(second_vacuum);
    EXPECT_EQ(second_vacuum->redirects_installed, 1U);
    EXPECT_EQ(pointer(*pool, relation_id, root), LinePointer::redirect(second_successor.slot_id()));
    EXPECT_EQ(pointer(*pool, relation_id, first_successor).state, LinePointerState::kUnused);
}

TEST(HeapMaintenanceTest, PruneWalFailureRestoresTheExactPageImage) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{404};
    auto table = HeapTable::create(relation_id, *pool, registry, log, 100).value();
    transaction::CommitLog commit_log;
    const auto root = table.insert(base::Xid{10}, 0, bytes("v1")).value();
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    const auto captured = mutation_snapshot(base::Xid{20});
    ASSERT_TRUE(table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("v2"),
            .indexed_columns_unchanged = true
        },
        mutation_context(base::Xid{20}, captured, commit_log)
    ));
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    const auto before = page_image(*pool, relation_id, root.page_id());
    log.clear();
    log.fail_next_prune();

    auto failed = table.vacuum(commit_log, transaction::kXidWrapBoundary);

    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(page_image(*pool, relation_id, root.page_id()), before);
    EXPECT_EQ(pointer(*pool, relation_id, root).state, LinePointerState::kNormal);
    EXPECT_EQ(log.operations(), (std::vector{HeapWalOperation::kPrune}));
}

TEST(HeapMaintenanceTest, RejectsInvalidHorizonBeforePinOrWalMutation) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{405}, *pool, registry, log).value();
    log.clear();

    auto invalid = table.vacuum(transaction::CommitLog{}, base::kInvalidXid);

    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_TRUE(log.operations().empty());
}

TEST(HeapMaintenanceTest, ConcurrentHotWritersAndReadersNeverLoseTheStableRoot) {
    auto pool = make_pool();
    ConcurrentHeapLog log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{408}, *pool, registry, log, 100).value();
    transaction::CommitLog commit_log;
    const std::vector<std::byte> payload(32U, std::byte{0x5A});
    const auto root = table.insert(base::Xid{10}, 0, payload).value();
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    ConcurrentHotStress stress{table, commit_log, root, payload};
    std::vector<std::thread> workers;
    workers.reserve(kConcurrentHotWriterCount + kConcurrentHotReaderCount);
    for (std::size_t writer = 0; writer < kConcurrentHotWriterCount; ++writer) {
        workers.emplace_back(run_concurrent_hot_writer, std::ref(stress));
    }
    for (std::size_t reader = 0; reader < kConcurrentHotReaderCount; ++reader) {
        workers.emplace_back(run_concurrent_hot_reader, std::ref(stress), reader);
    }
    stress.start.store(true, std::memory_order_release);
    join_concurrent_hot_workers(workers);

    EXPECT_EQ(stress.failures.load(std::memory_order_relaxed), 0U)
        << "vacuum_errors=" << stress.vacuum_errors.load(std::memory_order_relaxed)
        << " update_errors=" << stress.update_errors.load(std::memory_order_relaxed)
        << " cold_updates=" << stress.cold_updates.load(std::memory_order_relaxed)
        << " status_errors=" << stress.status_errors.load(std::memory_order_relaxed)
        << " retry_exhaustions=" << stress.retry_exhaustions.load(std::memory_order_relaxed)
        << " stale_target_retries=" << stress.stale_target_retries.load(std::memory_order_relaxed)
        << " reader_misses=" << stress.reader_misses.load(std::memory_order_relaxed)
        << " first_update_error=" << stress.first_update_error;
    EXPECT_EQ(
        stress.updates.load(std::memory_order_relaxed),
        kConcurrentHotWriterCount * kConcurrentHotUpdatesPerWriter
    );
    EXPECT_EQ(table.page_count(), 1U);
    const auto final_snapshot = concurrent_hot_snapshot(base::Xid{60'000});
    auto final = table.fetch(
        root,
        fetch_context(base::Xid{60'000}, final_snapshot, commit_log, transaction::kXidWrapBoundary)
    );
    ASSERT_TRUE(final);
    ASSERT_TRUE(final->has_value());
    EXPECT_EQ(final->value().payload, payload);
}

}  // namespace
}  // namespace db::access
