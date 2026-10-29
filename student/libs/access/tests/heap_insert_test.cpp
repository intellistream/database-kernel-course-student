#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/heap_page.hpp"
#include "db/access/heap_table.hpp"
#include "db/access/heap_wal.hpp"
#include "db/base/error.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/page.hpp"

namespace db::access {
namespace {

enum class TraceKind : std::uint8_t {
    kWal,
    kWalDurable,
    kData,
};

struct TraceEvent final {
    TraceKind kind;
    base::Lsn lsn;

    auto operator==(const TraceEvent&) const noexcept -> bool = default;
};

class MutationTrace final {
public:
    void record(const TraceEvent event) {
        const std::lock_guard lock{mutex_};
        events_.push_back(event);
    }

    auto snapshot() const -> std::vector<TraceEvent> {
        const std::lock_guard lock{mutex_};
        return events_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<TraceEvent> events_;
};

struct LoggedTuple final {
    base::SlotId slot_id;
    std::vector<std::byte> bytes;
};

struct LoggedMutation final {
    HeapWalOperation operation;
    base::Xid xid;
    storage::PageAddress address;
    bool will_initialize;
    bool has_before_page;
    base::Lsn page_lsn_before_log;
    base::Lsn result_lsn;
    bool succeeded;
    std::vector<LoggedTuple> tuples;
};

class ScriptedHeapLog final : public HeapMutationLogPort {
public:
    explicit ScriptedHeapLog(MutationTrace* const trace = nullptr) noexcept
        : trace_{trace} {}

    void fail_on(const HeapWalOperation operation, const std::size_t occurrence) {
        const std::lock_guard lock{mutex_};
        failure_ = Script{.operation = operation, .occurrence = occurrence};
    }

    void return_invalid_on(const HeapWalOperation operation, const std::size_t occurrence) {
        const std::lock_guard lock{mutex_};
        invalid_ = Script{.operation = operation, .occurrence = occurrence};
    }

    auto log(const HeapWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.after_page == nullptr) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "heap mutation is missing the after page",
                }
            );
        }
        const std::lock_guard lock{mutex_};
        const auto occurrence = ++operation_counts_[mutation.operation];
        const bool fail = matches(failure_, mutation.operation, occurrence);
        const bool invalid = matches(invalid_, mutation.operation, occurrence);
        const auto result_lsn = fail || invalid ? base::kInvalidLsn : base::Lsn{next_lsn_};
        if (!fail && !invalid) {
            next_lsn_ += 8U;
            if (trace_ != nullptr) {
                trace_->record(TraceEvent{TraceKind::kWal, result_lsn});
            }
        }

        LoggedMutation recorded{
            .operation = mutation.operation,
            .xid = mutation.xid,
            .address = mutation.address,
            .will_initialize = mutation.will_initialize,
            .has_before_page = mutation.before_page != nullptr,
            .page_lsn_before_log = mutation.after_page->lsn(),
            .result_lsn = result_lsn,
            .succeeded = !fail && !invalid,
            .tuples = {},
        };
        recorded.tuples.reserve(mutation.tuples.size());
        for (const auto& tuple : mutation.tuples) {
            recorded.tuples.push_back(
                LoggedTuple{
                    .slot_id = tuple.slot_id,
                    .bytes = {tuple.bytes.begin(), tuple.bytes.end()},
                }
            );
        }
        mutations_.push_back(std::move(recorded));

        if (fail) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kIo,
                    "injected heap WAL failure",
                }
            );
        }
        return result_lsn;
    }

    auto snapshot() const -> std::vector<LoggedMutation> {
        const std::lock_guard lock{mutex_};
        return mutations_;
    }

private:
    struct Script final {
        HeapWalOperation operation;
        std::size_t occurrence;
    };

    static auto matches(
        const std::optional<Script>& script,
        const HeapWalOperation operation,
        const std::size_t occurrence
    ) noexcept -> bool {
        return script.has_value() && script->operation == operation
               && script->occurrence == occurrence;
    }

    MutationTrace* trace_;
    mutable std::mutex mutex_;
    std::map<HeapWalOperation, std::size_t> operation_counts_;
    std::vector<LoggedMutation> mutations_;
    std::optional<Script> failure_;
    std::optional<Script> invalid_;
    std::uint64_t next_lsn_{8};
};

class TraceDurability final : public storage::PageDurabilityPort {
public:
    explicit TraceDurability(MutationTrace& trace) noexcept
        : trace_{trace} {}

    auto flush_through(const base::Lsn target_lsn) -> base::Status override {
        if (target_lsn == base::kInvalidLsn) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "WAL durability target must be valid",
                }
            );
        }
        trace_.record(TraceEvent{TraceKind::kWalDurable, target_lsn});
        return {};
    }

private:
    MutationTrace& trace_;
};

class TraceDisk final : public storage::DiskManager {
public:
    explicit TraceDisk(MutationTrace& trace) noexcept
        : trace_{trace} {}

    auto read_page(const storage::PageAddress address) -> base::Result<storage::Page> override {
        const std::lock_guard lock{mutex_};
        const auto found = pages_.find(key(address));
        if (found == pages_.end()) {
            return base::fail(base::Error{base::ErrorCode::kNotFound, "trace page not found"});
        }
        return storage::Page::load(found->second, address);
    }

    auto write_page(const storage::PageAddress address, const storage::Page& page)
        -> base::Status override {
        trace_.record(TraceEvent{TraceKind::kData, page.lsn()});
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
        trace_.record(TraceEvent{TraceKind::kData, page->lsn()});
        pages_[key(address)] = page->bytes();
        ++next;
        return address;
    }

    auto page_count(const base::RelationId relation_id) -> base::Result<std::uint64_t> override {
        const std::lock_guard lock{mutex_};
        const auto found = next_page_.find(relation_id.value());
        return found == next_page_.end() ? 0U : found->second;
    }

    auto sync_relation(const base::RelationId relation_id) -> base::Status override {
        static_cast<void>(relation_id);
        return {};
    }

    auto sync_all() -> base::Status override {
        return {};
    }

    auto remove_relation(const base::RelationId relation_id, const bool missing_ok)
        -> base::Status override {
        const std::lock_guard lock{mutex_};
        const auto count = next_page_.erase(relation_id.value());
        for (auto iterator = pages_.begin(); iterator != pages_.end();) {
            if (iterator->first.first == relation_id.value()) {
                iterator = pages_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        if (count == 0U && !missing_ok) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kNotFound,
                    "trace relation not found",
                }
            );
        }
        return {};
    }

private:
    using PageKey = std::pair<std::uint32_t, std::uint32_t>;

    static auto key(const storage::PageAddress address) noexcept -> PageKey {
        return {address.relation_id.value(), address.page_id.value()};
    }

    MutationTrace& trace_;
    std::mutex mutex_;
    std::map<PageKey, storage::Page::Storage> pages_;
    std::map<std::uint32_t, std::uint32_t> next_page_;
};

auto make_trace_pool(MutationTrace& trace, const std::size_t frames = 8)
    -> std::unique_ptr<storage::BufferPool> {
    auto pool = storage::BufferPool::create(
        frames,
        std::make_unique<TraceDisk>(trace),
        nullptr,
        std::make_unique<TraceDurability>(trace)
    );
    EXPECT_TRUE(pool) << pool.error().to_string();
    return std::move(pool).value();
}

auto operations(const ScriptedHeapLog& log, const HeapWalOperation operation)
    -> std::vector<LoggedMutation> {
    auto recorded = log.snapshot();
    std::erase_if(recorded, [operation](const LoggedMutation& mutation) {
        return mutation.operation != operation;
    });
    return recorded;
}

auto row_views(const std::vector<std::vector<std::byte>>& rows) -> std::vector<base::Bytes> {
    std::vector<base::Bytes> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        result.emplace_back(row);
    }
    return result;
}

TEST(HeapInsertTest, LogsBeforeAllocationAndFlushesWalBeforeTupleData) {
    MutationTrace trace;
    auto pool = make_trace_pool(trace);
    ScriptedHeapLog log{&trace};
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{81};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();

    const auto creation_events = trace.snapshot();
    ASSERT_GE(creation_events.size(), 2U);
    EXPECT_EQ(creation_events[0].kind, TraceKind::kWal);
    EXPECT_EQ(creation_events[1], (TraceEvent{TraceKind::kData, base::kInvalidLsn}));

    const std::array<std::byte, 4> payload{
        std::byte{0x11},
        std::byte{0x22},
        std::byte{0x33},
        std::byte{0x44},
    };
    const auto rid = table.insert(base::Xid{7}, 3, payload).value();
    EXPECT_EQ(rid, (base::Rid{base::PageId{0}, base::SlotId{0}}));

    const auto insert_records = operations(log, HeapWalOperation::kInsert);
    ASSERT_EQ(insert_records.size(), 1U);
    EXPECT_TRUE(insert_records[0].succeeded);
    EXPECT_TRUE(insert_records[0].has_before_page);
    EXPECT_FALSE(insert_records[0].will_initialize);
    EXPECT_EQ(insert_records[0].xid, base::Xid{7});
    ASSERT_EQ(insert_records[0].tuples.size(), 1U);
    EXPECT_EQ(insert_records[0].tuples[0].slot_id, rid.slot_id());

    const storage::PageAddress address{relation_id, rid.page_id()};
    auto read = pool->read_page(address);
    ASSERT_TRUE(read);
    EXPECT_EQ(read->page()->lsn(), insert_records[0].result_lsn);
    auto page_copy = *read->page();
    auto heap_page = HeapPage::open(page_copy, address).value();
    const auto tuple = heap_page.tuple(rid.slot_id()).value();
    EXPECT_EQ(tuple.header.xmin, base::Xid{7});
    EXPECT_EQ(tuple.header.command_id, 3U);
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), tuple.payload.begin()));

    ASSERT_TRUE(pool->flush_page(address));
    const auto events = trace.snapshot();
    const auto durable =
        std::ranges::find(events, TraceEvent{TraceKind::kWalDurable, insert_records[0].result_lsn});
    const auto data =
        std::ranges::find(events, TraceEvent{TraceKind::kData, insert_records[0].result_lsn});
    ASSERT_NE(durable, events.end());
    ASSERT_NE(data, events.end());
    EXPECT_LT(durable, data);
}

TEST(HeapInsertTest, InitializationWalFailureAndInvalidLsnDoNotAllocateAPage) {
    MutationTrace trace;
    auto pool = make_trace_pool(trace);
    ScriptedHeapLog log;
    log.fail_on(HeapWalOperation::kInitialize, 1);
    log.return_invalid_on(HeapWalOperation::kInitialize, 2);
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{87};

    auto failed = HeapTable::create(relation_id, *pool, registry, log);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(pool->relation_page_count(relation_id).value(), 0U);

    auto invalid = HeapTable::create(relation_id, *pool, registry, log);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvariantViolation);
    EXPECT_EQ(pool->relation_page_count(relation_id).value(), 0U);

    auto table = HeapTable::create(relation_id, *pool, registry, log);
    ASSERT_TRUE(table);
    EXPECT_EQ(table->page_count(), 1U);
    EXPECT_EQ(pool->relation_page_count(relation_id).value(), 1U);

    const auto records = operations(log, HeapWalOperation::kInitialize);
    ASSERT_EQ(records.size(), 3U);
    EXPECT_FALSE(records[0].succeeded);
    EXPECT_FALSE(records[1].succeeded);
    EXPECT_TRUE(records[2].succeeded);
}

TEST(HeapInsertTest, WalFailureAndInvalidLsnRestoreTheExactPageImage) {
    MutationTrace trace;
    auto pool = make_trace_pool(trace);
    ScriptedHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{82};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const storage::PageAddress address{relation_id, base::PageId{0}};
    const auto before = pool->read_page(address)->page()->bytes();
    const std::array<std::byte, 2> payload{std::byte{1}, std::byte{2}};

    log.fail_on(HeapWalOperation::kInsert, 1);
    auto failed = table.insert(base::Xid{8}, 0, payload);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(pool->read_page(address)->page()->bytes(), before);

    log.return_invalid_on(HeapWalOperation::kInsert, 2);
    auto invalid = table.insert(base::Xid{8}, 0, payload);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvariantViolation);
    EXPECT_EQ(pool->read_page(address)->page()->bytes(), before);

    const auto inserted = table.insert(base::Xid{8}, 0, payload);
    ASSERT_TRUE(inserted);
    EXPECT_EQ(inserted->slot_id(), base::SlotId{0});
}

TEST(HeapInsertTest, MultiInsertCoalescesNewPageInitializationWithItsFirstMutation) {
    MutationTrace trace;
    auto pool = make_trace_pool(trace, 16);
    ScriptedHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{83};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();

    std::vector<std::vector<std::byte>> rows;
    rows.reserve(24U);
    for (std::uint8_t value = 0; value < 24U; ++value) {
        rows.emplace_back(700U, static_cast<std::byte>(value));
    }
    const auto views = row_views(rows);
    auto inserted = table.multi_insert(base::Xid{9}, 7, views);
    ASSERT_TRUE(inserted);
    ASSERT_EQ(inserted->size(), rows.size());

    std::set<base::PageId> touched_pages;
    for (std::size_t index = 0; index < inserted->size(); ++index) {
        const auto rid = inserted->at(index);
        touched_pages.insert(rid.page_id());
        const storage::PageAddress address{relation_id, rid.page_id()};
        auto page_copy = *pool->read_page(address)->page();
        auto heap_page = HeapPage::open(page_copy, address).value();
        const auto tuple = heap_page.tuple(rid.slot_id()).value();
        EXPECT_EQ(tuple.header.xmin, base::Xid{9});
        EXPECT_EQ(tuple.header.command_id, 7U);
        EXPECT_EQ(tuple.payload.size(), rows[index].size());
        EXPECT_TRUE(std::equal(rows[index].begin(), rows[index].end(), tuple.payload.begin()));
    }
    ASSERT_GT(touched_pages.size(), 1U);

    const auto multi_records = operations(log, HeapWalOperation::kMultiInsert);
    EXPECT_EQ(multi_records.size(), touched_pages.size());
    EXPECT_EQ(operations(log, HeapWalOperation::kInitialize).size(), 1U);
    std::size_t logged_tuples = 0;
    for (const auto& record : multi_records) {
        EXPECT_TRUE(record.succeeded);
        EXPECT_EQ(record.will_initialize, record.address.page_id != base::PageId{0U});
        EXPECT_EQ(record.has_before_page, record.address.page_id == base::PageId{0U});
        EXPECT_TRUE(touched_pages.contains(record.address.page_id));
        logged_tuples += record.tuples.size();
    }
    EXPECT_EQ(logged_tuples, rows.size());
}

TEST(HeapInsertTest, ValidatesTheCompleteBoundedBatchBeforeMutation) {
    MutationTrace trace;
    auto pool = make_trace_pool(trace);
    ScriptedHeapLog log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{84}, *pool, registry, log).value();
    const auto initial_record_count = log.snapshot().size();

    std::vector<std::vector<std::byte>> too_many_storage(kMaximumHeapMultiInsertRows + 1U);
    const auto too_many = row_views(too_many_storage);
    auto row_bound = table.multi_insert(base::Xid{10}, 0, too_many);
    ASSERT_FALSE(row_bound);
    EXPECT_EQ(row_bound.error().code(), base::ErrorCode::kResourceExhausted);

    constexpr auto k_largest_payload =
        storage::kMaximumPageFreeSpace - kTupleHeaderSize - kLinePointerSize;
    const auto payload_rows = kMaximumHeapMultiInsertPayloadBytes / k_largest_payload + 1U;
    std::vector<std::vector<std::byte>> too_large_storage(
        payload_rows,
        std::vector<std::byte>(k_largest_payload)
    );
    const auto too_large = row_views(too_large_storage);
    auto byte_bound = table.multi_insert(base::Xid{10}, 0, too_large);
    ASSERT_FALSE(byte_bound);
    EXPECT_EQ(byte_bound.error().code(), base::ErrorCode::kResourceExhausted);

    std::vector<std::byte> oversized(k_largest_payload + 1U);
    EXPECT_EQ(
        table.insert(base::Xid{10}, 0, oversized).error().code(),
        base::ErrorCode::kResourceExhausted
    );
    const std::array<std::byte, 1> valid_payload{std::byte{1}};
    EXPECT_EQ(
        table.insert(base::kInvalidXid, 0, valid_payload).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_TRUE(table.multi_insert(base::kInvalidXid, 0, {}).value().empty());
    EXPECT_EQ(log.snapshot().size(), initial_record_count);
}

TEST(HeapInsertTest, FailedLaterMultiMutationKeepsEarlierLoggedPagesOnly) {
    MutationTrace trace;
    auto pool = make_trace_pool(trace, 8);
    ScriptedHeapLog log;
    log.fail_on(HeapWalOperation::kMultiInsert, 2);
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{85};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    std::vector<std::vector<std::byte>> rows(12U, std::vector<std::byte>(900U, std::byte{7}));
    const auto views = row_views(rows);

    auto inserted = table.multi_insert(base::Xid{11}, 0, views);
    ASSERT_FALSE(inserted);
    EXPECT_EQ(inserted.error().code(), base::ErrorCode::kIo);
    const auto records = operations(log, HeapWalOperation::kMultiInsert);
    ASSERT_EQ(records.size(), 2U);
    EXPECT_TRUE(records[0].succeeded);
    EXPECT_FALSE(records[1].succeeded);

    std::size_t resident_tuples = 0;
    for (const auto page_id : table.page_ids()) {
        const storage::PageAddress address{relation_id, page_id};
        auto page_copy = *pool->read_page(address)->page();
        resident_tuples += HeapPage::open(page_copy, address)->item_count();
    }
    EXPECT_EQ(resident_tuples, records[0].tuples.size());
    EXPECT_GT(resident_tuples, 0U);
    EXPECT_LT(resident_tuples, rows.size());
}

TEST(HeapInsertTest, ConcurrentInsertionsProduceUniqueSelfIdentifyingRids) {
    MutationTrace trace;
    auto pool = make_trace_pool(trace, 16);
    ScriptedHeapLog log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{86}, *pool, registry, log).value();
    constexpr std::size_t k_workers = 8;
    constexpr std::size_t k_rows_per_worker = 50;
    std::mutex result_mutex;
    std::vector<base::Rid> rids;
    std::vector<base::Error> errors;
    std::vector<std::thread> workers;
    workers.reserve(k_workers);
    for (std::size_t worker = 0; worker < k_workers; ++worker) {
        workers.emplace_back([&, worker] {
            const std::array<std::byte, 32> payload{static_cast<std::byte>(worker)};
            for (std::size_t row = 0; row < k_rows_per_worker; ++row) {
                auto inserted = table.insert(
                    base::Xid{static_cast<std::uint32_t>(worker + 1U)},
                    static_cast<std::uint32_t>(row),
                    payload
                );
                const std::lock_guard lock{result_mutex};
                if (inserted) {
                    rids.push_back(inserted.value());
                } else {
                    errors.push_back(std::move(inserted).error());
                    break;
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    ASSERT_TRUE(errors.empty());
    ASSERT_EQ(rids.size(), k_workers * k_rows_per_worker);
    EXPECT_EQ(std::set<base::Rid>(rids.begin(), rids.end()).size(), rids.size());
    EXPECT_EQ(operations(log, HeapWalOperation::kInsert).size(), rids.size());
}

}  // namespace
}  // namespace db::access
