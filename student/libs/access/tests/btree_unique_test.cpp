#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"
#include "db/access/heap_index_tid.hpp"
#include "db/access/heap_page.hpp"
#include "db/access/heap_table.hpp"
#include "db/access/heap_wal.hpp"
#include "db/access/index_tid.hpp"
#include "db/base/error.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/manager.hpp"
#include "db/transaction/ssi.hpp"
#include "db/transaction/xid_waiter.hpp"

#include "btree_test_log.hpp"

namespace db::access {
namespace {

class MonotonicHeapLog final : public HeapMutationLogPort {
public:
    auto log(const HeapWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.after_page == nullptr) {
            return base::fail(
                base::Error{base::ErrorCode::kInvalidArgument, "missing heap after-image"}
            );
        }
        const auto result = base::Lsn{next_lsn_};
        next_lsn_ += 8U;
        return result;
    }

private:
    std::uint64_t next_lsn_{8U};
};

class FixedTidClassifier final : public IndexTidClassifier {
public:
    explicit FixedTidClassifier(IndexTidState default_state = IndexTidState::live()) noexcept
        : default_state_{default_state} {}

    void set(const base::Rid rid, const IndexTidState state) {
        states_.insert_or_assign(rid, state);
    }

    auto classify_tid(const base::Rid rid) const -> base::Result<IndexTidState> override {
        const auto found = states_.find(rid);
        return found == states_.end() ? default_state_ : found->second;
    }

private:
    std::map<base::Rid, IndexTidState> states_;
    IndexTidState default_state_;
};

class NoWait final : public UniqueConflictWaiter {
public:
    auto wait_for(base::Xid blocking_xid) const -> base::Status override {
        static_cast<void>(blocking_xid);
        return {};
    }
};

enum class TestPortException : std::uint8_t {
    kAllocation,
    kLength,
    kStandard,
    kUnknown,
};

[[noreturn]] void throw_port_exception(const TestPortException exception) {
    switch (exception) {
        case TestPortException::kAllocation:
            throw std::bad_alloc{};
        case TestPortException::kLength:
            throw std::length_error{"injected unique port length failure"};
        case TestPortException::kStandard:
            throw std::runtime_error{"injected unique port exception"};
        case TestPortException::kUnknown:
            throw 7;
    }
    std::terminate();
}

class ThrowingTidClassifier final : public IndexTidClassifier {
public:
    explicit ThrowingTidClassifier(const TestPortException exception) noexcept
        : exception_{exception} {}

    auto classify_tid(const base::Rid rid) const -> base::Result<IndexTidState> override {
        static_cast<void>(rid);
        throw_port_exception(exception_);
    }

private:
    TestPortException exception_;
};

class ThrowingWaiter final : public UniqueConflictWaiter {
public:
    explicit ThrowingWaiter(const TestPortException exception) noexcept
        : exception_{exception} {}

    auto wait_for(const base::Xid blocking_xid) const -> base::Status override {
        static_cast<void>(blocking_xid);
        throw_port_exception(exception_);
    }

private:
    TestPortException exception_;
};

class SequencedTidClassifier final : public IndexTidClassifier {
public:
    explicit SequencedTidClassifier(base::Xid blocker) noexcept
        : blocker_{blocker} {}

    auto classify_tid(base::Rid rid) const -> base::Result<IndexTidState> override {
        static_cast<void>(rid);
        const auto call = calls_.fetch_add(1U, std::memory_order_relaxed);
        return call == 0U ? IndexTidState::in_progress(blocker_) : IndexTidState::dead();
    }

private:
    base::Xid blocker_;
    mutable std::atomic<std::size_t> calls_{0U};
};

class PinCheckingWaiter final : public UniqueConflictWaiter {
public:
    PinCheckingWaiter(
        storage::BufferPool& pool,
        base::RelationId relation_id,
        std::function<std::uint64_t()> page_count
    )
        : pool_{&pool},
          relation_id_{relation_id},
          page_count_{std::move(page_count)} {}

    auto wait_for(const base::Xid blocking_xid) const -> base::Status override {
        observed_blocker_ = blocking_xid;
        for (std::uint32_t page = 0U; page < page_count_(); ++page) {
            const auto address = storage::PageAddress{relation_id_, base::PageId{page}};
            if (!pool_->contains(address)) {
                continue;
            }
            auto snapshot = pool_->frame_snapshot(address);
            if (!snapshot) {
                return base::fail(std::move(snapshot).error());
            }
            if (snapshot->pin_count != 0U) {
                return base::fail(
                    base::Error{
                        base::ErrorCode::kInvariantViolation,
                        "unique conflict wait retained a B-tree page pin",
                    }
                );
            }
        }
        calls_.fetch_add(1U, std::memory_order_relaxed);
        return {};
    }

    auto calls() const noexcept -> std::size_t {
        return calls_.load(std::memory_order_relaxed);
    }

    auto observed_blocker() const noexcept -> base::Xid {
        return observed_blocker_;
    }

private:
    storage::BufferPool* pool_;
    base::RelationId relation_id_;
    std::function<std::uint64_t()> page_count_;
    mutable std::atomic<std::size_t> calls_{0U};
    mutable base::Xid observed_blocker_{base::kInvalidXid};
};

constexpr auto heap_rid(const std::uint32_t page_id, const std::uint16_t slot_id = 1U) noexcept
    -> base::Rid {
    return base::Rid{base::PageId{page_id}, base::SlotId{slot_id}};
}

auto make_pool(const test_support::TestDirectory& directory, const std::size_t frames = 64U)
    -> base::Result<std::unique_ptr<storage::BufferPool>> {
    auto disk = storage::FileDiskManager::open(directory.join("relations"));
    if (!disk) {
        return base::fail(std::move(disk).error());
    }
    return storage::BufferPool::create(
        frames,
        std::move(disk).value(),
        nullptr,
        test::btree_durability()
    );
}

auto make_unique_relation(storage::BufferPool& pool, const base::RelationId relation_id)
    -> base::Result<BtreeRelation> {
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    if (!layout) {
        return base::fail(std::move(layout).error());
    }
    return BtreeRelation::create(
        relation_id,
        pool,
        std::move(layout).value(),
        true,
        test::btree_log()
    );
}

auto insert_key(
    const BtreeRelation& relation,
    storage::BufferPool& pool,
    const types::Value& value,
    const base::Rid rid
) -> base::Status {
    const std::array values{value};
    return relation.insert(pool, std::span<const types::Value>{values}, rid);
}

auto insert_key(
    const BtreeRelation& relation,
    storage::BufferPool& pool,
    const types::Value& value,
    const base::Rid rid,
    const BtreeInsertContext context
) -> base::Status {
    const std::array values{value};
    return relation.insert(pool, std::span<const types::Value>{values}, rid, context);
}

auto scan_key(const BtreeRelation& relation, storage::BufferPool& pool, const types::Value& value)
    -> base::Result<std::vector<base::Rid>> {
    const std::array values{value};
    auto scan = BtreeScan::for_eq(relation, std::span<const types::Value>{values}, {});
    if (!scan) {
        return base::fail(std::move(scan).error());
    }
    std::vector<base::Rid> result;
    for (;;) {
        auto next = scan->next(pool);
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!next->has_value()) {
            return result;
        }
        result.push_back(next->value());
    }
}

void set_status(
    transaction::CommitLog& commit_log,
    const base::Xid xid,
    const transaction::TransactionStatus status
) {
    ASSERT_TRUE(commit_log.ensure_xid_allocated(xid));
    ASSERT_TRUE(commit_log.set_status(xid, status));
}

auto rewrite_tuple_header(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const base::Rid rid,
    const std::function<void(TupleHeader&)>& rewrite
) -> base::Status {
    const storage::PageAddress address{relation_id, rid.page_id()};
    auto write = pool.write_page(address);
    if (!write) {
        return base::fail(std::move(write).error());
    }
    auto page = HeapPage::open(*write->page(), address);
    if (!page) {
        return base::fail(std::move(page).error());
    }
    auto tuple = page->tuple(rid.slot_id());
    if (!tuple) {
        return base::fail(std::move(tuple).error());
    }
    auto header = tuple->header;
    rewrite(header);
    auto bytes = page->mutable_tuple_bytes(rid.slot_id());
    if (!bytes) {
        return base::fail(std::move(bytes).error());
    }
    return header.encode(bytes->first(kTupleHeaderSize));
}

TEST(BtreeUniqueTest, StructuralCheckRejectsNonNullDuplicatesAndAllowsNulls) {
    auto directory = test_support::TestDirectory::create("btree-unique-structural");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory);
    ASSERT_TRUE(pool);
    auto relation = make_unique_relation(**pool, base::RelationId{630});
    ASSERT_TRUE(relation);

    ASSERT_TRUE(insert_key(*relation, **pool, types::Value::integer(7), heap_rid(10U)));
    auto duplicate = insert_key(*relation, **pool, types::Value::integer(7), heap_rid(11U));
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kAlreadyExists);

    ASSERT_TRUE(
        insert_key(*relation, **pool, types::Value::null(types::kInt64Type), heap_rid(12U))
    );
    ASSERT_TRUE(
        insert_key(*relation, **pool, types::Value::null(types::kInt64Type), heap_rid(13U))
    );
    auto nulls = scan_key(*relation, **pool, types::Value::null(types::kInt64Type));
    ASSERT_TRUE(nulls);
    EXPECT_EQ(*nulls, (std::vector<base::Rid>{heap_rid(12U), heap_rid(13U)}));
}

TEST(BtreeUniqueTest, SerializableInsertConflictsAndSplitCopiesPageSiread) {
    auto directory = test_support::TestDirectory::create("btree-unique-ssi-split");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory);
    ASSERT_TRUE(pool);
    const auto relation_id = base::RelationId{638};
    auto relation = make_unique_relation(**pool, relation_id);
    ASSERT_TRUE(relation);

    ASSERT_TRUE(insert_key(*relation, **pool, types::Value::integer(0), heap_rid(1'000U)));
    auto old_root = relation->root(**pool);
    ASSERT_TRUE(old_root);
    ASSERT_TRUE(old_root->has_value());
    ASSERT_EQ(old_root->value().level, 0U);

    transaction::SerializableConflictManager conflicts;
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    constexpr base::Xid right_page_writer{300};
    const transaction::Snapshot snapshot{
        .xmin = reader,
        .xmax = base::Xid{301},
        .in_progress = {reader, writer, right_page_writer},
        .command_id = 1U,
    };
    for (const auto xid : {reader, writer, right_page_writer}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, snapshot));
    }
    ASSERT_TRUE(conflicts.record_read(
        reader,
        transaction::SsiReadRecord{
            .target = transaction::SsiReadTarget::for_page(relation_id, old_root->value().page_id),
        }
    ));

    FixedTidClassifier classifier;
    NoWait waiter;
    ASSERT_TRUE(insert_key(
        *relation,
        **pool,
        types::Value::integer(1),
        heap_rid(1'001U),
        BtreeInsertContext{
            .writer_xid = writer,
            .tid_classifier = &classifier,
            .unique_waiter = &waiter,
            .serializable_conflicts = &conflicts,
            .serializable_writer = true,
        }
    ));
    EXPECT_EQ(conflicts.outgoing_conflicts(reader), std::vector<base::Xid>({writer}));

    const BtreeInsertContext nonserializable_splitter{
        .writer_xid = base::Xid{400},
        .tid_classifier = &classifier,
        .unique_waiter = &waiter,
        .serializable_conflicts = &conflicts,
        .serializable_writer = false,
    };
    for (std::int64_t value = 2; value < 220; ++value) {
        ASSERT_TRUE(insert_key(
            *relation,
            **pool,
            types::Value::integer(value),
            heap_rid(static_cast<std::uint32_t>(1'000 + value)),
            nonserializable_splitter
        ));
    }

    auto root = relation->root(**pool);
    ASSERT_TRUE(root);
    ASSERT_TRUE(root->has_value());
    ASSERT_EQ(root->value().level, 1U);
    const storage::PageAddress root_address{relation_id, root->value().page_id};
    auto root_read = (*pool)->read_page(root_address);
    ASSERT_TRUE(root_read);
    auto root_page = BtreePage::open(*root_read->page(), root_address, relation->key_layout());
    ASSERT_TRUE(root_page);
    auto right_child = root_page->tuple(root_page->tuple_count() - 1U);
    ASSERT_TRUE(right_child);
    ASSERT_TRUE(right_child->child_page_id.has_value());

    ASSERT_TRUE(conflicts.check_write_conflict(
        right_page_writer,
        transaction::SsiReadTarget::for_page(relation_id, right_child->child_page_id.value())
    ));
    EXPECT_EQ(
        conflicts.outgoing_conflicts(reader),
        (std::vector<base::Xid>{writer, right_page_writer})
    );
}

TEST(BtreeUniqueTest, HeapProbeRetainsOldSnapshotsAndRetiresDeadEntries) {
    auto directory = test_support::TestDirectory::create("btree-unique-lifecycle");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory);
    ASSERT_TRUE(pool);
    auto relation = make_unique_relation(**pool, base::RelationId{631});
    ASSERT_TRUE(relation);
    NoWait waiter;
    FixedTidClassifier classifier{IndexTidState::retained()};
    const auto first = heap_rid(20U);
    const auto second = heap_rid(21U);
    const auto third = heap_rid(22U);

    ASSERT_TRUE(insert_key(*relation, **pool, types::Value::integer(7), first));
    ASSERT_TRUE(insert_key(
        *relation,
        **pool,
        types::Value::integer(7),
        second,
        BtreeInsertContext{base::Xid{11}, &classifier, &waiter}
    ));
    classifier.set(first, IndexTidState::dead());
    classifier.set(second, IndexTidState::retained());
    ASSERT_TRUE(insert_key(
        *relation,
        **pool,
        types::Value::integer(7),
        third,
        BtreeInsertContext{base::Xid{12}, &classifier, &waiter}
    ));

    auto rows = scan_key(*relation, **pool, types::Value::integer(7));
    ASSERT_TRUE(rows);
    EXPECT_EQ(*rows, (std::vector<base::Rid>{second, third}));
    classifier.set(second, IndexTidState::live());
    auto duplicate = insert_key(
        *relation,
        **pool,
        types::Value::integer(7),
        heap_rid(23U),
        BtreeInsertContext{base::Xid{13}, &classifier, &waiter}
    );
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kAlreadyExists);
}

TEST(BtreeUniqueTest, HeapProbeChecksAnEqualRunAcrossOlderLeaves) {
    auto directory = test_support::TestDirectory::create("btree-unique-cross-leaf");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory, 128U);
    ASSERT_TRUE(pool);
    auto relation = make_unique_relation(**pool, base::RelationId{632});
    ASSERT_TRUE(relation);
    NoWait waiter;
    FixedTidClassifier classifier{IndexTidState::retained()};
    constexpr std::size_t k_versions = 180U;
    for (std::size_t version = 0U; version < k_versions; ++version) {
        const auto rid = heap_rid(static_cast<std::uint32_t>(1'000U + version));
        ASSERT_TRUE(insert_key(
            *relation,
            **pool,
            types::Value::integer(99),
            rid,
            BtreeInsertContext{
                base::Xid{static_cast<std::uint32_t>(100U + version)},
                &classifier,
                &waiter,
            }
        ));
    }
    auto existing = scan_key(*relation, **pool, types::Value::integer(99));
    ASSERT_TRUE(existing);
    ASSERT_EQ(existing->size(), k_versions);
    classifier.set(existing->front(), IndexTidState::live());

    auto duplicate = insert_key(
        *relation,
        **pool,
        types::Value::integer(99),
        heap_rid(2'000U),
        BtreeInsertContext{base::Xid{900}, &classifier, &waiter}
    );
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kAlreadyExists);
}

TEST(BtreeUniqueTest, WaitsOnlyAfterReleasingEveryBtreePageGuardThenRetries) {
    auto directory = test_support::TestDirectory::create("btree-unique-wait-outside-latch");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory);
    ASSERT_TRUE(pool);
    const auto relation_id = base::RelationId{633};
    auto relation = make_unique_relation(**pool, relation_id);
    ASSERT_TRUE(relation);
    const auto old_rid = heap_rid(30U);
    const auto new_rid = heap_rid(31U);
    ASSERT_TRUE(insert_key(*relation, **pool, types::Value::integer(5), old_rid));
    SequencedTidClassifier classifier{base::Xid{44}};
    PinCheckingWaiter waiter{
        **pool,
        relation_id,
        [&] {
            return pool.value()->relation_page_count(relation_id).value();
        },
    };

    auto inserted = insert_key(
        *relation,
        **pool,
        types::Value::integer(5),
        new_rid,
        BtreeInsertContext{base::Xid{45}, &classifier, &waiter}
    );
    ASSERT_TRUE(inserted) << inserted.error().to_string();
    EXPECT_EQ(waiter.calls(), 1U);
    EXPECT_EQ(waiter.observed_blocker(), base::Xid{44});
    auto rows = scan_key(*relation, **pool, types::Value::integer(5));
    ASSERT_TRUE(rows);
    EXPECT_EQ(*rows, (std::vector<base::Rid>{new_rid}));
}

TEST(BtreeUniqueTest, ConvertsClassifierAndWaiterExceptionsWithoutChangingTheIndex) {
    auto directory = test_support::TestDirectory::create("btree-unique-port-exceptions");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory);
    ASSERT_TRUE(pool);
    auto relation = make_unique_relation(**pool, base::RelationId{640});
    ASSERT_TRUE(relation);
    const auto old_rid = heap_rid(40U);
    ASSERT_TRUE(insert_key(*relation, **pool, types::Value::integer(5), old_rid));

    ThrowingTidClassifier classifier{TestPortException::kStandard};
    NoWait no_wait;
    auto classified = insert_key(
        *relation,
        **pool,
        types::Value::integer(5),
        heap_rid(41U),
        BtreeInsertContext{base::Xid{50}, &classifier, &no_wait}
    );
    ASSERT_FALSE(classified);
    EXPECT_EQ(classified.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(classified.error().to_string().find("unique port exception"), std::string::npos);

    SequencedTidClassifier blocking_classifier{base::Xid{51}};
    ThrowingWaiter waiter{TestPortException::kAllocation};
    auto waited = insert_key(
        *relation,
        **pool,
        types::Value::integer(5),
        heap_rid(42U),
        BtreeInsertContext{base::Xid{52}, &blocking_classifier, &waiter}
    );
    ASSERT_FALSE(waited);
    EXPECT_EQ(waited.error().code(), base::ErrorCode::kResourceExhausted);

    auto rows = scan_key(*relation, **pool, types::Value::integer(5));
    ASSERT_TRUE(rows);
    EXPECT_EQ(*rows, std::vector<base::Rid>{old_rid});
}

TEST(IndexTidClassifierTest, DefaultBatchConvertsEveryExceptionAndPreservesOutput) {
    constexpr std::array cases{
        std::pair{TestPortException::kAllocation, base::ErrorCode::kResourceExhausted},
        std::pair{TestPortException::kLength, base::ErrorCode::kResourceExhausted},
        std::pair{TestPortException::kStandard, base::ErrorCode::kInternal},
        std::pair{TestPortException::kUnknown, base::ErrorCode::kInternal},
    };
    const std::array rids{heap_rid(1U)};

    for (const auto& [exception, expected_code] : cases) {
        ThrowingTidClassifier classifier{exception};
        std::vector states{IndexTidState::retained()};
        auto classified = classifier.classify_tids(rids, states);
        ASSERT_FALSE(classified);
        EXPECT_EQ(classified.error().code(), expected_code);
        EXPECT_EQ(states, std::vector{IndexTidState::retained()});
    }
}

TEST(BtreeUniqueTest, ConcurrentSameKeyInsertionHasExactlyOneWinner) {
    auto directory = test_support::TestDirectory::create("btree-unique-concurrent");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory, 128U);
    ASSERT_TRUE(pool);
    auto relation = make_unique_relation(**pool, base::RelationId{634});
    ASSERT_TRUE(relation);
    for (std::int64_t key = 0; key < 240; ++key) {
        ASSERT_TRUE(insert_key(
            *relation,
            **pool,
            types::Value::integer(key),
            heap_rid(static_cast<std::uint32_t>(3'000 + key))
        ));
    }

    constexpr std::size_t k_writers = 8U;
    std::atomic<std::size_t> ready{0U};
    std::atomic<bool> start{false};
    std::atomic<std::size_t> inserted{0U};
    std::atomic<std::size_t> duplicates{0U};
    std::atomic<std::size_t> unexpected{0U};
    std::vector<std::thread> workers;
    workers.reserve(k_writers);
    for (std::size_t writer = 0U; writer < k_writers; ++writer) {
        workers.emplace_back([&, writer] {
            ready.fetch_add(1U, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            auto result = insert_key(
                *relation,
                **pool,
                types::Value::integer(240),
                heap_rid(static_cast<std::uint32_t>(4'000U + writer))
            );
            if (result) {
                inserted.fetch_add(1U, std::memory_order_relaxed);
            } else if (result.error().code() == base::ErrorCode::kAlreadyExists) {
                duplicates.fetch_add(1U, std::memory_order_relaxed);
            } else {
                unexpected.fetch_add(1U, std::memory_order_relaxed);
            }
        });
    }
    while (ready.load(std::memory_order_acquire) != k_writers) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) {
        worker.join();
    }
    EXPECT_EQ(inserted.load(std::memory_order_relaxed), 1U);
    EXPECT_EQ(duplicates.load(std::memory_order_relaxed), k_writers - 1U);
    EXPECT_EQ(unexpected.load(std::memory_order_relaxed), 0U);
}

TEST(HeapIndexTidClassifierTest, ClassifiesCreatorAndRemoverLifecycle) {
    auto directory = test_support::TestDirectory::create("heap-index-tid-lifecycle");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory);
    ASSERT_TRUE(pool);
    HeapSpaceRegistry registry;
    MonotonicHeapLog log;
    const auto relation_id = base::RelationId{640};
    auto table = HeapTable::create(relation_id, **pool, registry, log);
    ASSERT_TRUE(table);
    transaction::CommitLog commit_log;
    transaction::TransactionManager waiter_manager;
    const auto& xid_waiter = waiter_manager.xid_waiter();

    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{11}, transaction::TransactionStatus::kAborted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{12}));
    auto live = table->insert(base::Xid{10}, 1U, {});
    auto dead = table->insert(base::Xid{11}, 1U, {});
    auto pending = table->insert(base::Xid{12}, 1U, {});
    ASSERT_TRUE(live);
    ASSERT_TRUE(dead);
    ASSERT_TRUE(pending);

    auto classifier = HeapIndexTidClassifier::create(
        *table,
        commit_log,
        xid_waiter,
        base::Xid{50},
        base::Xid{30}
    );
    ASSERT_TRUE(classifier);
    EXPECT_EQ(classifier->classify_tid(*live).value(), IndexTidState::live());
    EXPECT_EQ(classifier->classify_tid(*dead).value(), IndexTidState::dead());
    EXPECT_EQ(
        classifier->classify_tid(*pending).value(),
        IndexTidState::in_progress(base::Xid{12})
    );

    set_status(commit_log, base::Xid{13}, transaction::TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{14}, transaction::TransactionStatus::kAborted);
    auto removed = table->insert(base::Xid{13}, 1U, {});
    ASSERT_TRUE(removed);
    ASSERT_TRUE(rewrite_tuple_header(**pool, relation_id, *removed, [](TupleHeader& header) {
        header.xmax = base::Xid{14};
        header.flags |= tuple_flags::kDeleted;
    }));
    EXPECT_EQ(classifier->classify_tid(*removed).value(), IndexTidState::live());

    set_status(commit_log, base::Xid{15}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(rewrite_tuple_header(**pool, relation_id, *removed, [](TupleHeader& header) {
        header.xmax = base::Xid{15};
    }));
    auto retained_classifier = HeapIndexTidClassifier::create(
        *table,
        commit_log,
        xid_waiter,
        base::Xid{50},
        base::Xid{15}
    );
    ASSERT_TRUE(retained_classifier);
    EXPECT_EQ(retained_classifier->classify_tid(*removed).value(), IndexTidState::retained());
    EXPECT_EQ(classifier->classify_tid(*removed).value(), IndexTidState::dead());
}

TEST(HeapIndexTidClassifierTest, MovingTransfersCapturedHorizonAndMakesSourceInert) {
    auto directory = test_support::TestDirectory::create("heap-index-tid-move");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory);
    ASSERT_TRUE(pool);
    HeapSpaceRegistry registry;
    MonotonicHeapLog log;
    auto table = HeapTable::create(base::RelationId{644}, **pool, registry, log);
    ASSERT_TRUE(table);
    transaction::CommitLog commit_log;
    transaction::TransactionManager waiter_manager;
    const auto& xid_waiter = waiter_manager.xid_waiter();
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    auto rid = table->insert(base::Xid{10}, 1U, {});
    ASSERT_TRUE(rid);

    auto first = HeapIndexTidClassifier::create(
        *table,
        commit_log,
        xid_waiter,
        base::Xid{50},
        base::Xid{30}
    );
    ASSERT_TRUE(first);
    HeapIndexTidClassifier moved{std::move(first).value()};
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.classify_tid(*rid).value(), IndexTidState::live());
    // NOLINTNEXTLINE(bugprone-use-after-move) - verifies the documented inert source.
    EXPECT_FALSE(first->valid());
    // NOLINTNEXTLINE(bugprone-use-after-move) - moved-from classification must not dereference.
    EXPECT_EQ(first->classify_tid(*rid).error().code(), base::ErrorCode::kUnavailable);
    // NOLINTNEXTLINE(bugprone-use-after-move) - moved-from waiting must not dereference.
    EXPECT_EQ(first->wait_for(base::Xid{10}).error().code(), base::ErrorCode::kUnavailable);

    auto second = HeapIndexTidClassifier::create(
        *table,
        commit_log,
        xid_waiter,
        base::Xid{51},
        base::Xid{30}
    );
    ASSERT_TRUE(second);
    moved = std::move(second).value();
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.classify_tid(*rid).value(), IndexTidState::live());
    // NOLINTNEXTLINE(bugprone-use-after-move) - verifies move-assignment source cleanup.
    EXPECT_FALSE(second->valid());
}

TEST(HeapIndexTidClassifierTest, AccumulatesRemovabilityAcrossAHotChain) {
    auto directory = test_support::TestDirectory::create("heap-index-tid-hot-chain");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory);
    ASSERT_TRUE(pool);
    HeapSpaceRegistry registry;
    MonotonicHeapLog log;
    const auto relation_id = base::RelationId{641};
    auto table = HeapTable::create(relation_id, **pool, registry, log);
    ASSERT_TRUE(table);
    transaction::CommitLog commit_log;
    transaction::TransactionManager waiter_manager;
    const auto& xid_waiter = waiter_manager.xid_waiter();
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{21}, transaction::TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{22}, transaction::TransactionStatus::kCommitted);
    auto root = table->insert(base::Xid{20}, 1U, {});
    auto successor = table->insert(base::Xid{21}, 2U, {});
    ASSERT_TRUE(root);
    ASSERT_TRUE(successor);
    ASSERT_EQ(root->page_id(), successor->page_id());
    ASSERT_TRUE(rewrite_tuple_header(**pool, relation_id, *root, [&](TupleHeader& header) {
        header.xmax = base::Xid{21};
        header.ctid = *successor;
        header.flags |= tuple_flags::kUpdated | tuple_flags::kHotUpdated;
    }));
    ASSERT_TRUE(rewrite_tuple_header(**pool, relation_id, *successor, [](TupleHeader& header) {
        header.xmax = base::Xid{22};
        header.flags |= tuple_flags::kHeapOnly | tuple_flags::kDeleted;
    }));

    auto retained = HeapIndexTidClassifier::create(
        *table,
        commit_log,
        xid_waiter,
        base::Xid{50},
        base::Xid{22}
    );
    auto dead = HeapIndexTidClassifier::create(
        *table,
        commit_log,
        xid_waiter,
        base::Xid{50},
        base::Xid{30}
    );
    ASSERT_TRUE(retained);
    ASSERT_TRUE(dead);
    EXPECT_EQ(retained->classify_tid(*root).value(), IndexTidState::retained());
    EXPECT_EQ(dead->classify_tid(*root).value(), IndexTidState::dead());

    auto own_insert = table->insert(base::Xid{50}, 3U, {});
    auto older = table->insert(base::Xid{20}, 3U, {});
    ASSERT_TRUE(own_insert);
    ASSERT_TRUE(older);
    ASSERT_TRUE(rewrite_tuple_header(**pool, relation_id, *own_insert, [](TupleHeader& header) {
        header.xmax = base::Xid{50};
        header.flags |= tuple_flags::kDeleted;
    }));
    ASSERT_TRUE(rewrite_tuple_header(**pool, relation_id, *older, [](TupleHeader& header) {
        header.xmax = base::Xid{50};
        header.flags |= tuple_flags::kDeleted;
    }));
    EXPECT_EQ(dead->classify_tid(*own_insert).value(), IndexTidState::dead());
    EXPECT_EQ(dead->classify_tid(*older).value(), IndexTidState::retained());
}

TEST(BtreeUniqueTest, RealHeapConflictWaitsForAbortAndReusesTheKey) {
    auto directory = test_support::TestDirectory::create("btree-unique-real-heap-wait");
    ASSERT_TRUE(directory);
    auto pool = make_pool(*directory);
    ASSERT_TRUE(pool);
    HeapSpaceRegistry registry;
    MonotonicHeapLog log;
    const auto heap_relation_id = base::RelationId{642};
    const auto index_relation_id = base::RelationId{643};
    auto table = HeapTable::create(heap_relation_id, **pool, registry, log);
    auto index = make_unique_relation(**pool, index_relation_id);
    ASSERT_TRUE(table);
    ASSERT_TRUE(index);
    transaction::TransactionManager manager;
    auto blocker_lease = manager.register_session();
    auto writer_lease = manager.register_session();
    ASSERT_TRUE(blocker_lease);
    ASSERT_TRUE(writer_lease);
    auto blocker_xid = manager.assign_xid(*blocker_lease);
    auto writer_xid = manager.assign_xid(*writer_lease);
    ASSERT_TRUE(blocker_xid);
    ASSERT_TRUE(writer_xid);
    const auto& commit_log = manager.commit_log();
    const auto& xid_waiter = manager.xid_waiter();
    auto old_rid = table->insert(*blocker_xid, 1U, {});
    auto new_rid = table->insert(*writer_xid, 1U, {});
    ASSERT_TRUE(old_rid);
    ASSERT_TRUE(new_rid);
    ASSERT_TRUE(insert_key(*index, **pool, types::Value::integer(1), *old_rid));
    auto classifier = HeapIndexTidClassifier::create(
        *table,
        commit_log,
        xid_waiter,
        *writer_xid,
        base::Xid{writer_xid->value() + 1U}
    );
    ASSERT_TRUE(classifier);
    std::atomic<bool> succeeded{false};
    std::atomic<bool> finished{false};
    std::thread inserter{[&] {
        succeeded.store(
            static_cast<bool>(insert_key(
                *index,
                **pool,
                types::Value::integer(1),
                *new_rid,
                BtreeInsertContext{*writer_xid, &*classifier, &*classifier}
            )),
            std::memory_order_release
        );
        finished.store(true, std::memory_order_release);
    }};
    while (xid_waiter.stats().waiting_transactions == 0U
           && !finished.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    const bool blocked = !finished.load(std::memory_order_acquire);
    bool released_index_pins = true;
    for (std::uint32_t page = 0U;
         page < pool.value()->relation_page_count(index_relation_id).value();
         ++page) {
        const auto snapshot = pool.value()->frame_snapshot(
            storage::PageAddress{index_relation_id, base::PageId{page}}
        );
        released_index_pins = released_index_pins && snapshot && snapshot->pin_count == 0U;
    }
    const auto aborted =
        manager.complete(*blocker_lease, *blocker_xid, transaction::TransactionStatus::kAborted);
    inserter.join();
    EXPECT_TRUE(blocked);
    EXPECT_TRUE(released_index_pins);
    ASSERT_TRUE(aborted);
    EXPECT_TRUE(succeeded.load(std::memory_order_acquire));
    auto rows = scan_key(*index, **pool, types::Value::integer(1));
    ASSERT_TRUE(rows);
    EXPECT_EQ(*rows, (std::vector<base::Rid>{*new_rid}));
    ASSERT_TRUE(
        manager.complete(*writer_lease, *writer_xid, transaction::TransactionStatus::kCommitted)
    );
    ASSERT_TRUE(blocker_lease->close());
    ASSERT_TRUE(writer_lease->close());
}

}  // namespace
}  // namespace db::access
