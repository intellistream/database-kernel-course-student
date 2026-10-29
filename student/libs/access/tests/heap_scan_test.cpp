#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/heap_page.hpp"
#include "db/access/heap_scan.hpp"
#include "db/access/heap_table.hpp"
#include "db/access/heap_wal.hpp"
#include "db/base/error.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/durability.hpp"
#include "db/storage/page.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/ssi.hpp"

namespace db::access {
namespace {

class MemoryDisk final : public storage::DiskManager {
public:
    auto read_page(const storage::PageAddress address) -> base::Result<storage::Page> override {
        const std::lock_guard lock{mutex_};
        const auto found = pages_.find(key(address));
        if (found == pages_.end()) {
            return base::fail(base::Error{base::ErrorCode::kNotFound, "heap scan page not found"});
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
                base::Error{base::ErrorCode::kNotFound, "heap scan relation not found"}
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
    auto flush_through(base::Lsn target_lsn) -> base::Status override {
        if (target_lsn == base::kInvalidLsn) {
            return base::fail(
                base::Error{base::ErrorCode::kInvalidArgument, "invalid durability target"}
            );
        }
        return {};
    }
};

class MonotonicHeapLog final : public HeapMutationLogPort {
public:
    auto log(const HeapWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.after_page == nullptr) {
            return base::fail(
                base::Error{base::ErrorCode::kInvalidArgument, "missing heap after-image"}
            );
        }
        const std::lock_guard lock{mutex_};
        const auto result = base::Lsn{next_lsn_};
        next_lsn_ += 8U;
        return result;
    }

private:
    std::mutex mutex_;
    std::uint64_t next_lsn_{8};
};

auto make_pool(const std::size_t frames) -> std::unique_ptr<storage::BufferPool> {
    auto pool = storage::BufferPool::create(
        frames,
        std::make_unique<MemoryDisk>(),
        nullptr,
        std::make_unique<AcceptDurability>()
    );
    EXPECT_TRUE(pool) << pool.error().to_string();
    return std::move(pool).value();
}

auto bytes(const std::string& text) -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const auto character : text) {
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

class PayloadEqualsQualification final : public HeapScanQualificationPort {
public:
    explicit PayloadEqualsQualification(std::string expected) noexcept
        : expected_{std::move(expected)} {}

    auto matches(const std::span<const std::byte> payload) const -> base::Result<bool> override {
        if (payload.size() != expected_.size()) {
            return false;
        }
        for (std::size_t index = 0U; index < payload.size(); ++index) {
            if (std::to_integer<unsigned char>(payload[index])
                != static_cast<unsigned char>(expected_[index])) {
                return false;
            }
        }
        return true;
    }

private:
    std::string expected_;
};

class ThrowingQualification final : public HeapScanQualificationPort {
public:
    auto matches(const std::span<const std::byte> payload) const -> base::Result<bool> override {
        static_cast<void>(payload);
        throw std::runtime_error{"injected heap qualification exception"};
    }
};

void set_status(
    transaction::CommitLog& commit_log,
    const base::Xid xid,
    const transaction::TransactionStatus status
) {
    ASSERT_TRUE(commit_log.ensure_xid_allocated(xid));
    ASSERT_TRUE(commit_log.set_status(xid, status));
}

auto snapshot(
    const std::uint32_t xmin,
    const std::uint32_t xmax,
    std::vector<base::Xid> in_progress = {},
    const std::uint32_t command_id = 1
) -> transaction::Snapshot {
    return transaction::Snapshot{
        .xmin = base::Xid{xmin},
        .xmax = base::Xid{xmax},
        .in_progress = std::move(in_progress),
        .command_id = command_id,
    };
}

auto scan_context(
    const base::Xid own_xid,
    const transaction::Snapshot& captured,
    const transaction::CommitLog& commit_log,
    transaction::SerializableConflictManager* const serializable_conflicts = nullptr,
    const HeapScanSireadMode siread_mode = HeapScanSireadMode::kPhysical,
    const HeapScanQualificationPort* const logical_qualification = nullptr
) noexcept -> HeapScanContext {
    return HeapScanContext{
        .mvcc =
            HeapMvccContext{
                .own_xid = own_xid,
                .snapshot = &captured,
                .commit_log = &commit_log,
                .serializable_conflicts = serializable_conflicts,
            },
        .siread_mode = siread_mode,
        .logical_qualification = logical_qualification,
    };
}

template <typename Change>
void rewrite_header(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const base::Rid rid,
    Change change
) {
    const storage::PageAddress address{relation_id, rid.page_id()};
    auto write = pool.write_page(address);
    ASSERT_TRUE(write);
    auto page = HeapPage::open(*write->page(), address);
    ASSERT_TRUE(page);
    auto tuple = page->mutable_tuple_bytes(rid.slot_id());
    ASSERT_TRUE(tuple);
    auto header = TupleHeader::decode(tuple->first(kTupleHeaderSize));
    ASSERT_TRUE(header);
    change(header.value());
    ASSERT_TRUE(header->encode(tuple->first(kTupleHeaderSize)));
}

template <typename Result>
void expect_unavailable(const Result& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inactive_heap_scan(HeapScan& scan) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(scan.valid());
    EXPECT_EQ(scan.relation_id(), base::kInvalidRelationId);
    EXPECT_EQ(scan.relation_page_horizon(), 0U);
    EXPECT_EQ(scan.pages_loaded(), 0U);
    expect_unavailable(scan.next());
    HeapScanTuple output;
    expect_unavailable(scan.next_into(output));
}

TEST(HeapScanTest, CapturesOneStableCandidateBatchAndReleasesThePagePin) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{91};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    const auto first_payload = bytes("alpha");
    const auto second_payload = bytes("bravo");
    ASSERT_TRUE(table.insert(base::Xid{5}, 0, first_payload));
    const auto second = table.insert(base::Xid{5}, 0, second_payload).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(6, 20);
    auto scan = HeapScan::begin(table, scan_context(base::Xid{9}, captured, commit_log)).value();

    auto first = scan.next().value();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(text(first->payload), "alpha");
    const storage::PageAddress address{relation_id, base::PageId{0}};
    EXPECT_EQ(pool->frame_snapshot(address)->pin_count, 0U);

    const auto replacement = bytes("xxxxx");
    {
        auto write = pool->write_page(address);
        ASSERT_TRUE(write);
        auto page = HeapPage::open(*write->page(), address);
        ASSERT_TRUE(page);
        auto tuple = page->mutable_tuple_bytes(second.slot_id());
        ASSERT_TRUE(tuple);
        std::copy(replacement.begin(), replacement.end(), tuple->begin() + kTupleHeaderSize);
    }

    auto snapshotted_second = scan.next().value();
    ASSERT_TRUE(snapshotted_second.has_value());
    EXPECT_EQ(text(snapshotted_second->payload), "bravo");
    EXPECT_FALSE(scan.next().value().has_value());
    EXPECT_EQ(scan.pages_loaded(), 1U);
    EXPECT_EQ(pool->frame_snapshot(address)->pin_count, 0U);

    auto later_scan =
        HeapScan::begin(table, scan_context(base::Xid{9}, captured, commit_log)).value();
    ASSERT_TRUE(later_scan.next().value().has_value());
    auto later_second = later_scan.next().value();
    ASSERT_TRUE(later_second.has_value());
    EXPECT_EQ(text(later_second->payload), "xxxxx");
}

TEST(HeapScanTest, NextIntoReusesPayloadCapacityAndRetainsOutputAtEnd) {
    auto pool = make_pool(4U);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{104U}, *pool, registry, heap_log).value();
    ASSERT_TRUE(table.insert(base::Xid{5U}, 0U, bytes("alpha")));
    ASSERT_TRUE(table.insert(base::Xid{5U}, 0U, bytes("bravo")));
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{5U}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(6U, 20U);
    auto scan = HeapScan::begin(table, scan_context(base::Xid{9U}, captured, commit_log)).value();

    HeapScanTuple output;
    auto first = scan.next_into(output);
    ASSERT_TRUE(first);
    ASSERT_TRUE(first.value());
    EXPECT_EQ(text(output.payload), "alpha");
    const std::byte* const allocation = output.payload.data();

    auto second = scan.next_into(output);
    ASSERT_TRUE(second);
    ASSERT_TRUE(second.value());
    EXPECT_EQ(text(output.payload), "bravo");
    EXPECT_EQ(output.payload.data(), allocation);

    auto end = scan.next_into(output);
    ASSERT_TRUE(end);
    EXPECT_FALSE(end.value());
    EXPECT_EQ(text(output.payload), "bravo");
}

TEST(HeapScanTest, MoveTransfersTheCursorAndLeavesTheSourceInert) {
    auto pool = make_pool(4U);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{103};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    transaction::CommitLog commit_log;
    const auto captured = snapshot(6U, 20U);
    auto source = HeapScan::begin(table, scan_context(base::Xid{9}, captured, commit_log)).value();

    HeapScan moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.relation_id(), relation_id);
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_heap_scan(source);

    auto assigned =
        HeapScan::begin(table, scan_context(base::Xid{9}, captured, commit_log)).value();
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    EXPECT_FALSE(assigned.next().value().has_value());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_heap_scan(moved);
}

TEST(HeapScanTest, MapsHeapHeadersToSnapshotAndCommitLogVisibility) {
    auto pool = make_pool(8);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{92};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();

    ASSERT_TRUE(table.insert(base::Xid{5}, 0, bytes("committed")));
    ASSERT_TRUE(table.insert(base::Xid{6}, 0, bytes("aborted")));
    ASSERT_TRUE(table.insert(base::Xid{8}, 0, bytes("snapshot-running")));
    ASSERT_TRUE(table.insert(base::Xid{9}, 0, bytes("own-before")));
    ASSERT_TRUE(table.insert(base::Xid{9}, 1, bytes("own-current")));
    ASSERT_TRUE(table.insert(base::Xid{12}, 0, bytes("after-horizon")));
    const auto deleted = table.insert(base::Xid{4}, 0, bytes("deleted")).value();
    const auto old_visible = table.insert(base::Xid{3}, 0, bytes("old-visible")).value();

    rewrite_header(*pool, relation_id, deleted, [](TupleHeader& header) {
        header.xmax = base::Xid{7};
        header.flags = tuple_flags::kDeleted;
    });
    rewrite_header(*pool, relation_id, old_visible, [](TupleHeader& header) {
        header.xmax = base::Xid{8};
        header.flags = tuple_flags::kUpdated;
    });

    transaction::CommitLog commit_log;
    for (const auto xid :
         {base::Xid{3}, base::Xid{4}, base::Xid{5}, base::Xid{7}, base::Xid{8}, base::Xid{12}}) {
        set_status(commit_log, xid, transaction::TransactionStatus::kCommitted);
    }
    set_status(commit_log, base::Xid{6}, transaction::TransactionStatus::kAborted);
    const auto captured = snapshot(5, 10, {base::Xid{8}}, 1);
    auto scan = HeapScan::begin(table, scan_context(base::Xid{9}, captured, commit_log)).value();
    std::vector<std::string> visible;
    while (true) {
        auto next = scan.next();
        ASSERT_TRUE(next);
        if (!next->has_value()) {
            break;
        }
        visible.push_back(text(next->value().payload));
    }

    EXPECT_EQ(visible, (std::vector<std::string>{"committed", "own-before", "old-visible"}));
}

TEST(HeapScanTest, ConsumerCanRejectAVisibleTupleAndContinue) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{96}, *pool, registry, heap_log).value();
    ASSERT_TRUE(table.insert(base::Xid{5}, 0, bytes("reject")));
    const auto accepted = table.insert(base::Xid{5}, 0, bytes("accept")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(6, 20);
    auto scan = HeapScan::begin(table, scan_context(base::Xid{9}, captured, commit_log)).value();

    std::optional<HeapScanTuple> result;
    while (true) {
        auto next = scan.next();
        ASSERT_TRUE(next);
        if (!next->has_value() || text(next->value().payload) == "accept") {
            result = std::move(next).value();
            break;
        }
    }
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rid, accepted);
}

TEST(HeapScanTest, SkipsRedirectsAndReturnsTheNormalSuccessorOnlyOnce) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{98};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    const auto old = table.insert(base::Xid{5}, 0, bytes("old")).value();
    const auto successor = table.insert(base::Xid{5}, 0, bytes("successor")).value();
    const auto ordinary = table.insert(base::Xid{5}, 0, bytes("ordinary")).value();
    const storage::PageAddress address{relation_id, old.page_id()};
    {
        auto pin = pool->pin_page(address);
        ASSERT_TRUE(pin);
        auto cleanup = pin->try_cleanup_latch();
        ASSERT_TRUE(cleanup);
        auto page = HeapPage::open(*cleanup->page(), address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page->redirect(old.slot_id(), successor.slot_id()));
        auto successor_bytes = page->mutable_tuple_bytes(successor.slot_id());
        ASSERT_TRUE(successor_bytes);
        auto successor_header = TupleHeader::decode(successor_bytes->first(kTupleHeaderSize));
        ASSERT_TRUE(successor_header);
        successor_header->flags = tuple_flags::kHeapOnly;
        ASSERT_TRUE(successor_header->encode(successor_bytes->first(kTupleHeaderSize)));
    }

    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(6, 20);
    auto scan = HeapScan::begin(table, scan_context(base::Xid{9}, captured, commit_log)).value();
    std::vector<base::Rid> found;
    while (true) {
        auto next = scan.next();
        ASSERT_TRUE(next);
        if (!next->has_value()) {
            break;
        }
        found.push_back(next->value().rid);
    }
    EXPECT_EQ(found, (std::vector<base::Rid>{successor, ordinary}));
}

TEST(HeapScanTest, FixesTheRelationHorizonAtBegin) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{93}, *pool, registry, heap_log).value();
    const std::vector<std::byte> first_payload(3000U, std::byte{0x11});
    const std::vector<std::byte> second_payload(3000U, std::byte{0x22});
    ASSERT_TRUE(table.insert(base::Xid{5}, 0, first_payload));
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(6, 20);
    auto scan = HeapScan::begin(table, scan_context(base::Xid{9}, captured, commit_log)).value();
    EXPECT_EQ(scan.relation_page_horizon(), 1U);
    ASSERT_TRUE(scan.next().value().has_value());

    ASSERT_TRUE(table.insert(base::Xid{5}, 0, second_payload));
    EXPECT_EQ(table.page_count(), 2U);
    EXPECT_FALSE(scan.next().value().has_value());

    auto fresh_scan =
        HeapScan::begin(table, scan_context(base::Xid{9}, captured, commit_log)).value();
    EXPECT_EQ(fresh_scan.relation_page_horizon(), 2U);
}

TEST(HeapScanTest, ScansRelationsLargerThanTheBufferPoolOnePageAtATime) {
    auto pool = make_pool(2);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{94}, *pool, registry, heap_log).value();
    constexpr std::size_t k_rows = 80;
    for (std::size_t row = 0; row < k_rows; ++row) {
        const std::vector<std::byte> payload(700U, static_cast<std::byte>(row));
        ASSERT_TRUE(table.insert(base::Xid{5}, 0, payload));
    }
    ASSERT_GE(table.page_count(), pool->pool_size() * 8U);
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(6, 20);
    auto scan = HeapScan::begin(table, scan_context(base::Xid{9}, captured, commit_log)).value();
    std::size_t rows = 0;
    std::vector<bool> seen(k_rows, false);
    for (;;) {
        auto tuple = scan.next().value();
        if (!tuple.has_value()) {
            break;
        }
        ASSERT_EQ(tuple->payload.size(), 700U);
        const auto value = std::to_integer<std::size_t>(tuple->payload.front());
        ASSERT_LT(value, seen.size());
        EXPECT_FALSE(seen[value]);
        seen[value] = true;
        ++rows;
    }
    EXPECT_EQ(rows, k_rows);
    EXPECT_TRUE(std::ranges::all_of(seen, [](const bool value) {
        return value;
    }));
    EXPECT_EQ(scan.pages_loaded(), scan.relation_page_horizon());
}

TEST(HeapScanTest, SerializablePhysicalScanLocksTheRelationAndRecordsInvisibleCreators) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{95}, *pool, registry, heap_log).value();
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    ASSERT_TRUE(table.insert(writer, 0, bytes("uncommitted")));
    transaction::CommitLog commit_log;
    ASSERT_TRUE(commit_log.ensure_xid_allocated(writer));
    const auto captured = snapshot(100, 201, {reader, writer});
    transaction::SerializableConflictManager conflicts;
    for (const auto xid : {reader, writer}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, captured));
    }

    auto scan =
        HeapScan::begin(table, scan_context(reader, captured, commit_log, &conflicts)).value();
    EXPECT_EQ(conflicts.read_lock_count(reader), 1U);
    EXPECT_FALSE(scan.next().value().has_value());
    EXPECT_EQ(conflicts.outgoing_conflicts(reader), std::vector<base::Xid>{writer});
}

TEST(HeapScanTest, SerializableVisibleOldVersionRecordsConcurrentUpdater) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{96};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    constexpr base::Xid creator{5};
    constexpr base::Xid reader{100};
    constexpr base::Xid updater{200};
    const auto old_version = table.insert(creator, 0, bytes("visible-old-version")).value();
    ASSERT_TRUE(table.insert(creator, 0, bytes("another-visible-version")));
    rewrite_header(*pool, relation_id, old_version, [updater](TupleHeader& header) {
        header.xmax = updater;
        header.flags = tuple_flags::kUpdated;
    });

    transaction::CommitLog commit_log;
    set_status(commit_log, creator, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(updater));
    const auto captured = snapshot(100, 201, {reader, updater});
    transaction::SerializableConflictManager conflicts;
    for (const auto xid : {reader, updater}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, captured));
    }

    auto scan =
        HeapScan::begin(table, scan_context(reader, captured, commit_log, &conflicts)).value();
    std::vector<std::string> visible;
    while (true) {
        auto next = scan.next();
        ASSERT_TRUE(next) << next.error().to_string();
        if (!next->has_value()) {
            break;
        }
        visible.push_back(text(next->value().payload));
    }
    EXPECT_EQ(
        visible,
        (std::vector<std::string>{"visible-old-version", "another-visible-version"})
    );
    EXPECT_EQ(conflicts.read_lock_count(reader), 1U);
    EXPECT_EQ(conflicts.outgoing_conflicts(reader), std::vector<base::Xid>{updater});

    constexpr base::Xid relation_writer{201};
    ASSERT_TRUE(conflicts.register_transaction(relation_writer));
    ASSERT_TRUE(conflicts.set_snapshot(relation_writer, captured));
    ASSERT_TRUE(conflicts.check_write_conflict(
        relation_writer,
        transaction::SsiReadTarget::for_tuple(relation_id, old_version)
    ));
    EXPECT_EQ(
        conflicts.outgoing_conflicts(reader),
        (std::vector<base::Xid>{updater, relation_writer})
    );
}

TEST(HeapScanTest, LogicalPredicateModeOmitsPhysicalLocksAndPreservesVisibleOldUpdater) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{98};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    constexpr base::Xid creator{5};
    constexpr base::Xid reader{100};
    constexpr base::Xid updater{200};
    const auto old_version = table.insert(creator, 0, bytes("visible-old-version")).value();
    rewrite_header(*pool, relation_id, old_version, [updater](TupleHeader& header) {
        header.xmax = updater;
        header.flags = tuple_flags::kUpdated;
    });

    transaction::CommitLog commit_log;
    set_status(commit_log, creator, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(updater));
    const auto captured = snapshot(100, 201, {reader, updater});
    transaction::SerializableConflictManager conflicts;
    for (const auto xid : {reader, updater}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, captured));
    }
    const PayloadEqualsQualification qualification{"visible-old-version"};

    auto scan = HeapScan::begin(
                    table,
                    scan_context(
                        reader,
                        captured,
                        commit_log,
                        &conflicts,
                        HeapScanSireadMode::kLogicalPredicate,
                        &qualification
                    )
    )
                    .value();
    EXPECT_EQ(conflicts.read_lock_count(reader), 0U);
    auto visible = scan.next();
    ASSERT_TRUE(visible) << visible.error().to_string();
    ASSERT_TRUE(visible->has_value());
    EXPECT_EQ(conflicts.read_lock_count(reader), 0U);
    EXPECT_EQ(conflicts.outgoing_conflicts(reader), std::vector<base::Xid>{updater});
}

TEST(HeapScanTest, LogicalPredicateModeRejectsUnrelatedVisibleVersionProvenance) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{99};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    constexpr base::Xid creator{5};
    constexpr base::Xid reader{100};
    constexpr base::Xid updater{200};
    const auto rejected = table.insert(creator, 0, bytes("id=2")).value();
    const auto matching = table.insert(creator, 0, bytes("id=1")).value();
    for (const base::Rid rid : {rejected, matching}) {
        rewrite_header(*pool, relation_id, rid, [updater](TupleHeader& header) {
            header.xmax = updater;
            header.flags = tuple_flags::kUpdated;
        });
    }

    transaction::CommitLog commit_log;
    set_status(commit_log, creator, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(updater));
    const auto captured = snapshot(100, 201, {reader, updater});
    transaction::SerializableConflictManager conflicts;
    for (const auto xid : {reader, updater}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, captured));
    }
    const PayloadEqualsQualification qualification{"id=1"};
    auto scan = HeapScan::begin(
                    table,
                    scan_context(
                        reader,
                        captured,
                        commit_log,
                        &conflicts,
                        HeapScanSireadMode::kLogicalPredicate,
                        &qualification
                    )
    )
                    .value();

    auto first = scan.next();
    ASSERT_TRUE(first) << first.error().to_string();
    ASSERT_TRUE(first->has_value());
    EXPECT_EQ(text(first->value().payload), "id=2");
    EXPECT_TRUE(conflicts.outgoing_conflicts(reader).empty());

    auto second = scan.next();
    ASSERT_TRUE(second) << second.error().to_string();
    ASSERT_TRUE(second->has_value());
    EXPECT_EQ(text(second->value().payload), "id=1");
    EXPECT_EQ(conflicts.outgoing_conflicts(reader), std::vector<base::Xid>{updater});
}

TEST(HeapScanTest, LogicalPredicateModeRejectsUnrelatedInvisibleVersionProvenance) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{100};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    ASSERT_TRUE(table.insert(writer, 0, bytes("id=2")));

    transaction::CommitLog commit_log;
    ASSERT_TRUE(commit_log.ensure_xid_allocated(writer));
    const auto captured = snapshot(100, 201, {reader, writer});
    transaction::SerializableConflictManager conflicts;
    for (const auto xid : {reader, writer}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, captured));
    }
    const PayloadEqualsQualification qualification{"id=1"};
    auto scan = HeapScan::begin(
                    table,
                    scan_context(
                        reader,
                        captured,
                        commit_log,
                        &conflicts,
                        HeapScanSireadMode::kLogicalPredicate,
                        &qualification
                    )
    )
                    .value();

    auto next = scan.next();
    ASSERT_TRUE(next) << next.error().to_string();
    EXPECT_FALSE(next->has_value());
    EXPECT_TRUE(conflicts.outgoing_conflicts(reader).empty());
}

TEST(HeapScanTest, LogicalPredicateModeRecordsMatchingInvisibleVersionProvenance) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{101};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    ASSERT_TRUE(table.insert(writer, 0, bytes("id=1")));

    transaction::CommitLog commit_log;
    ASSERT_TRUE(commit_log.ensure_xid_allocated(writer));
    const auto captured = snapshot(100, 201, {reader, writer});
    transaction::SerializableConflictManager conflicts;
    for (const auto xid : {reader, writer}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, captured));
    }
    const PayloadEqualsQualification qualification{"id=1"};
    auto scan = HeapScan::begin(
                    table,
                    scan_context(
                        reader,
                        captured,
                        commit_log,
                        &conflicts,
                        HeapScanSireadMode::kLogicalPredicate,
                        &qualification
                    )
    )
                    .value();

    auto next = scan.next();
    ASSERT_TRUE(next) << next.error().to_string();
    EXPECT_FALSE(next->has_value());
    EXPECT_EQ(conflicts.outgoing_conflicts(reader), std::vector<base::Xid>{writer});
}

TEST(HeapScanTest, LogicalPredicateModeRequiresQualificationPort) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{102};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    constexpr base::Xid reader{100};
    const auto captured = snapshot(100, 101, {reader});
    transaction::CommitLog commit_log;
    transaction::SerializableConflictManager conflicts;

    auto scan = HeapScan::begin(
        table,
        scan_context(
            reader,
            captured,
            commit_log,
            &conflicts,
            HeapScanSireadMode::kLogicalPredicate
        )
    );

    ASSERT_FALSE(scan);
    EXPECT_EQ(scan.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(HeapScanTest, DefersQualificationAndRunsItAfterReleasingThePageLatch) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{104};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    ASSERT_TRUE(table.insert(base::Xid{5}, 0, bytes("visible")));
    ASSERT_TRUE(table.insert(writer, 0, bytes("id=1")));

    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(writer));
    const auto captured = snapshot(100, 201, {reader, writer});
    transaction::SerializableConflictManager conflicts;
    for (const auto xid : {reader, writer}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, captured));
    }
    const ThrowingQualification qualification;
    const auto address = storage::PageAddress{relation_id, base::PageId{0}};
    {
        auto scan = HeapScan::begin(
                        table,
                        scan_context(
                            reader,
                            captured,
                            commit_log,
                            &conflicts,
                            HeapScanSireadMode::kLogicalPredicate,
                            &qualification
                        )
        )
                        .value();

        auto visible = scan.next();
        ASSERT_TRUE(visible) << visible.error().to_string();
        ASSERT_TRUE(visible->has_value());
        EXPECT_EQ(text(visible->value().payload), "visible");
        EXPECT_EQ(pool->frame_snapshot(address)->pin_count, 0U);

        auto failed = scan.next();
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error().code(), base::ErrorCode::kInternal);
        EXPECT_NE(
            failed.error().to_string().find("injected heap qualification exception"),
            std::string::npos
        );
        EXPECT_TRUE(conflicts.outgoing_conflicts(reader).empty());
        EXPECT_EQ(pool->frame_snapshot(address)->pin_count, 0U);
        auto write = pool->write_page(address);
        ASSERT_TRUE(write) << write.error().to_string();
    }
    EXPECT_EQ(pool->frame_snapshot(address)->pin_count, 0U);
}

TEST(HeapScanTest, EmptySerializableScanRelationLockCoversAFutureInsert) {
    auto pool = make_pool(4);
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{97};
    auto table = HeapTable::create(relation_id, *pool, registry, heap_log).value();
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    const auto captured = snapshot(100, 201, {reader, writer});
    transaction::CommitLog commit_log;
    transaction::SerializableConflictManager conflicts;
    for (const auto xid : {reader, writer}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, captured));
    }

    auto scan =
        HeapScan::begin(table, scan_context(reader, captured, commit_log, &conflicts)).value();
    EXPECT_FALSE(scan.next().value().has_value());
    EXPECT_EQ(conflicts.read_lock_count(reader), 1U);

    const auto inserted = table.insert(writer, 0, bytes("phantom")).value();
    ASSERT_TRUE(conflicts.check_write_conflict(
        writer,
        transaction::SsiReadTarget::for_tuple(relation_id, inserted)
    ));
    EXPECT_EQ(conflicts.outgoing_conflicts(reader), std::vector<base::Xid>{writer});
}

}  // namespace
}  // namespace db::access
