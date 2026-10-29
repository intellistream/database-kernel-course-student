#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/transaction/manager.hpp"
#include "db/transaction/ssi.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction {
namespace {

auto snapshot(std::uint32_t xmin, std::uint32_t xmax, std::vector<base::Xid> in_progress = {})
    -> Snapshot {
    return Snapshot{
        .xmin = base::Xid{xmin},
        .xmax = base::Xid{xmax},
        .in_progress = std::move(in_progress),
        .command_id = 1
    };
}

void begin_serializable(
    SerializableConflictManager& manager,
    base::Xid xid,
    const Snapshot& captured
) {
    ASSERT_TRUE(manager.register_transaction(xid));
    ASSERT_TRUE(manager.set_snapshot(xid, captured));
}

auto tuple_target(std::uint32_t relation, std::uint32_t page, std::uint16_t slot) -> SsiReadTarget {
    return SsiReadTarget::for_tuple(
        base::RelationId{relation},
        base::Rid{base::PageId{page}, base::SlotId{slot}}
    );
}

auto read_record(SsiReadTarget target, base::Xid conflict_xid = base::kInvalidXid)
    -> SsiReadRecord {
    return SsiReadRecord{
        .target = target,
        .creator_xid = base::Xid{5},
        .conflict_xid = conflict_xid
    };
}

auto contains(const std::vector<base::Xid>& values, base::Xid xid) -> bool {
    return std::ranges::find(values, xid) != values.end();
}

class IntegerEqualsPredicateMatcher final : public SsiPredicateMatcher {
public:
    explicit IntegerEqualsPredicateMatcher(const std::int64_t expected) noexcept
        : expected_{expected} {}

    auto matches(const std::span<const types::Value> values) const -> base::Result<bool> override {
        if (values.size() != 1U || values.front().data_type() != types::kInt64Type
            || values.front().as_integer() == nullptr) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kExecution,
                    "test predicate requires exactly one INTEGER value",
                }
            );
        }
        return *values.front().as_integer() == expected_;
    }

private:
    std::int64_t expected_;
};

class FailingPredicateMatcher final : public SsiPredicateMatcher {
public:
    auto matches([[maybe_unused]] const std::span<const types::Value> values) const
        -> base::Result<bool> override {
        return base::fail(
            base::Error{
                base::ErrorCode::kExecution,
                "test predicate evaluation failed",
            }
        );
    }
};

TEST(SsiReadTargetTest, OwnsValidityEqualityAndHashSemantics) {
    const auto tuple = tuple_target(1U, 2U, 3U);
    const auto same_tuple = tuple_target(1U, 2U, 3U);
    const auto page = SsiReadTarget::for_page(base::RelationId{1U}, base::PageId{2U});
    const auto relation = SsiReadTarget::for_relation(base::RelationId{1U});

    EXPECT_TRUE(tuple.valid());
    EXPECT_TRUE(page.valid());
    EXPECT_TRUE(relation.valid());
    EXPECT_EQ(std::hash<SsiReadTarget>{}(tuple), std::hash<SsiReadTarget>{}(same_tuple));

    const std::unordered_set<SsiReadTarget> targets{tuple, same_tuple, page, relation};
    EXPECT_EQ(targets.size(), 3U);
    EXPECT_TRUE(targets.contains(tuple));
    EXPECT_TRUE(targets.contains(page));
    EXPECT_TRUE(targets.contains(relation));
}

TEST(SerializableConflictManagerTest, ValidatesRegistrationTargetsAndFixedSnapshots) {
    SerializableConflictManager manager;
    auto invalid = manager.register_transaction(base::kInvalidXid);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);

    constexpr base::Xid xid{100};
    auto before_registration = manager.set_snapshot(xid, snapshot(100, 101));
    ASSERT_FALSE(before_registration);
    EXPECT_EQ(before_registration.error().code(), base::ErrorCode::kTransactionState);
    auto registered = manager.register_transaction(xid);
    ASSERT_TRUE(registered);
    EXPECT_EQ(*registered, 1U);
    auto duplicate = manager.register_transaction(xid);
    ASSERT_TRUE(duplicate);
    EXPECT_EQ(*duplicate, *registered);
    auto no_snapshot_commit = manager.precommit(xid);
    ASSERT_FALSE(no_snapshot_commit);
    EXPECT_EQ(no_snapshot_commit.error().code(), base::ErrorCode::kTransactionState);

    const auto captured = snapshot(100, 201, {base::Xid{200}});
    ASSERT_TRUE(manager.set_snapshot(xid, captured));
    auto same_set_new_command = captured;
    same_set_new_command.command_id = 99;
    EXPECT_TRUE(manager.set_snapshot(xid, same_set_new_command));
    auto changed = manager.set_snapshot(xid, snapshot(100, 202, {base::Xid{200}}));
    ASSERT_FALSE(changed);
    EXPECT_EQ(changed.error().code(), base::ErrorCode::kTransactionState);

    const SsiReadTarget malformed{
        .granularity = SsiLockGranularity::kTuple,
        .relation_id = base::RelationId{1},
        .page_id = base::PageId{2},
        .slot_id = base::kInvalidSlotId
    };
    EXPECT_FALSE(malformed.valid());
    auto bad_read = manager.record_read(xid, read_record(malformed));
    ASSERT_FALSE(bad_read);
    EXPECT_EQ(bad_read.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(SerializableConflictManagerTest, RecordsSymmetricTupleEdgesOnceAndNeverSelfConflicts) {
    SerializableConflictManager manager;
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    begin_serializable(manager, reader, snapshot(100, 201, {writer}));
    begin_serializable(manager, writer, snapshot(100, 201, {reader}));
    const auto target = tuple_target(1, 4, 2);

    ASSERT_TRUE(manager.record_read(reader, read_record(target)));
    ASSERT_TRUE(manager.record_read(reader, read_record(target)));
    EXPECT_EQ(manager.read_lock_count(reader), 1U);
    ASSERT_TRUE(manager.check_write_conflict(writer, target));
    ASSERT_TRUE(manager.check_write_conflict(writer, target));
    EXPECT_EQ(manager.outgoing_conflicts(reader), std::vector<base::Xid>({writer}));
    EXPECT_EQ(manager.incoming_conflicts(writer), std::vector<base::Xid>({reader}));
    EXPECT_TRUE(manager.did_write(writer));

    ASSERT_TRUE(manager.check_write_conflict(reader, target));
    EXPECT_FALSE(contains(manager.outgoing_conflicts(reader), reader));
}

TEST(SerializableConflictManagerTest, IndependentTargetsDoNotCreateFalsePositiveEdges) {
    SerializableConflictManager manager;
    constexpr base::Xid reader{100};
    constexpr base::Xid independent_writer{200};
    constexpr base::Xid conflicting_writer{300};
    const auto overlap = snapshot(100, 301, {reader, independent_writer, conflicting_writer});
    begin_serializable(manager, reader, overlap);
    begin_serializable(manager, independent_writer, overlap);
    begin_serializable(manager, conflicting_writer, overlap);

    const auto observed = tuple_target(1, 4, 2);
    ASSERT_TRUE(manager.record_read(reader, read_record(observed)));
    ASSERT_TRUE(manager.check_write_conflict(independent_writer, tuple_target(2, 4, 2)));
    EXPECT_TRUE(manager.outgoing_conflicts(reader).empty());

    ASSERT_TRUE(manager.check_write_conflict(conflicting_writer, observed));
    EXPECT_EQ(manager.outgoing_conflicts(reader), std::vector<base::Xid>({conflicting_writer}));
}

TEST(SerializableConflictManagerTest, PageRelationAndSplitLocksProtectCoarserRanges) {
    SerializableConflictManager manager;
    constexpr base::Xid reader{100};
    constexpr base::Xid page_writer{200};
    constexpr base::Xid relation_writer{300};
    const auto overlap = snapshot(100, 301, {reader, page_writer, relation_writer});
    begin_serializable(manager, reader, overlap);
    begin_serializable(manager, page_writer, overlap);
    begin_serializable(manager, relation_writer, overlap);

    ASSERT_TRUE(manager.record_read(
        reader,
        read_record(SsiReadTarget::for_page(base::RelationId{1}, base::PageId{5}))
    ));
    ASSERT_TRUE(
        manager.copy_page_lock_after_split(base::RelationId{1}, base::PageId{5}, base::PageId{6})
    );
    ASSERT_TRUE(manager.check_write_conflict(
        page_writer,
        SsiReadTarget::for_page(base::RelationId{1}, base::PageId{6})
    ));
    EXPECT_TRUE(contains(manager.outgoing_conflicts(reader), page_writer));

    ASSERT_TRUE(
        manager.record_read(reader, read_record(SsiReadTarget::for_relation(base::RelationId{2})))
    );
    ASSERT_TRUE(manager.record_read(reader, read_record(tuple_target(2, 99, 7))));
    ASSERT_TRUE(manager.check_write_conflict(relation_writer, tuple_target(2, 88, 1)));
    EXPECT_TRUE(contains(manager.outgoing_conflicts(reader), relation_writer));
    EXPECT_EQ(manager.read_lock_count(reader), 3U);
}

TEST(SerializableConflictManagerTest, PromotesBoundedTupleAndPageLockSets) {
    SerializableConflictManager manager;
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    const auto overlap = snapshot(100, 201, {reader, writer});
    begin_serializable(manager, reader, overlap);
    begin_serializable(manager, writer, overlap);

    for (std::uint16_t slot = 0; slot < kSsiTupleLocksPerPage; ++slot) {
        ASSERT_TRUE(manager.record_read(reader, read_record(tuple_target(1, 4, slot))));
    }
    EXPECT_EQ(manager.read_lock_count(reader), 1U);
    ASSERT_TRUE(manager.check_write_conflict(writer, tuple_target(1, 4, 99)));
    EXPECT_TRUE(contains(manager.outgoing_conflicts(reader), writer));

    for (std::uint32_t page = 0; page < kSsiPageLocksPerRelation; ++page) {
        ASSERT_TRUE(manager.record_read(
            reader,
            read_record(SsiReadTarget::for_page(base::RelationId{2}, base::PageId{page}))
        ));
    }
    EXPECT_EQ(manager.read_lock_count(reader), 2U);
    ASSERT_TRUE(manager.record_read(reader, read_record(tuple_target(2, 1000, 1))));
    EXPECT_EQ(manager.read_lock_count(reader), 2U);
}

TEST(SerializableConflictManagerTest, ReadSideEdgesRequireSnapshotOverlapAndSsiWriter) {
    SerializableConflictManager manager;
    constexpr base::Xid reader{100};
    constexpr base::Xid concurrent_writer{200};
    constexpr base::Xid old_writer{50};
    begin_serializable(manager, reader, snapshot(100, 201, {concurrent_writer}));
    begin_serializable(manager, concurrent_writer, snapshot(100, 201, {reader}));
    begin_serializable(manager, old_writer, snapshot(50, 51));

    ASSERT_TRUE(manager.record_invisible_conflict(reader, concurrent_writer));
    EXPECT_TRUE(contains(manager.outgoing_conflicts(reader), concurrent_writer));
    ASSERT_TRUE(manager.record_invisible_conflict(reader, old_writer));
    EXPECT_FALSE(contains(manager.outgoing_conflicts(reader), old_writer));
    ASSERT_TRUE(manager.record_invisible_conflict(reader, base::Xid{300}));
    EXPECT_EQ(manager.outgoing_conflicts(reader).size(), 1U);
}

TEST(SerializableConflictManagerTest, LogicalPredicatesMatchOldAndNewRowsWithoutPhysicalLocks) {
    SerializableConflictManager manager;
    constexpr base::Xid reader{100};
    constexpr base::Xid nonmatching_writer{200};
    constexpr base::Xid matching_writer{300};
    const auto overlap = snapshot(100, 301, {reader, nonmatching_writer, matching_writer});
    for (const auto xid : {reader, nonmatching_writer, matching_writer}) {
        begin_serializable(manager, xid, overlap);
    }

    auto predicate = SsiPredicate::matcher(std::make_shared<IntegerEqualsPredicateMatcher>(7));
    ASSERT_TRUE(predicate);
    ASSERT_TRUE(
        manager.record_predicate_read(reader, base::RelationId{1}, std::move(predicate).value())
    );
    EXPECT_EQ(manager.read_lock_count(reader), 0U);
    EXPECT_EQ(manager.predicate_lock_count(reader), 1U);

    const std::vector nonmatching{types::Value::integer(6)};
    ASSERT_TRUE(manager.check_predicate_write_conflict(
        nonmatching_writer,
        base::RelationId{1},
        std::nullopt,
        std::span<const types::Value>{nonmatching}
    ));
    EXPECT_TRUE(manager.outgoing_conflicts(reader).empty());

    const std::vector old_values{types::Value::integer(6)};
    const std::vector new_values{types::Value::integer(7)};
    ASSERT_TRUE(manager.check_predicate_write_conflict(
        matching_writer,
        base::RelationId{1},
        std::span<const types::Value>{old_values},
        std::span<const types::Value>{new_values}
    ));
    EXPECT_EQ(manager.outgoing_conflicts(reader), std::vector<base::Xid>({matching_writer}));
    EXPECT_TRUE(manager.did_write(matching_writer));
}

TEST(SerializableConflictManagerTest, LogicalPredicatesPromoteAndPreserveWholeRelationCoverage) {
    SerializableConflictManager manager;
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    const auto overlap = snapshot(100, 201, {reader, writer});
    begin_serializable(manager, reader, overlap);
    begin_serializable(manager, writer, overlap);

    for (std::size_t index = 0U; index < kSsiPredicatesPerRelation; ++index) {
        auto predicate = SsiPredicate::matcher(
            std::make_shared<IntegerEqualsPredicateMatcher>(static_cast<std::int64_t>(index))
        );
        ASSERT_TRUE(predicate);
        ASSERT_TRUE(
            manager.record_predicate_read(reader, base::RelationId{1}, std::move(predicate).value())
        );
    }
    EXPECT_EQ(manager.predicate_lock_count(reader), 1U);

    const std::vector values{types::Value::integer(999)};
    ASSERT_TRUE(manager.check_predicate_write_conflict(
        writer,
        base::RelationId{1},
        std::nullopt,
        std::span<const types::Value>{values}
    ));
    EXPECT_EQ(manager.outgoing_conflicts(reader), std::vector<base::Xid>({writer}));
}

TEST(SerializableConflictManagerTest, LogicalPredicateErrorsAndInvalidRowsRemainStructured) {
    SerializableConflictManager manager;
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    const auto overlap = snapshot(100, 201, {reader, writer});
    begin_serializable(manager, reader, overlap);
    begin_serializable(manager, writer, overlap);

    auto absent_matcher = SsiPredicate::matcher(nullptr);
    ASSERT_FALSE(absent_matcher);
    EXPECT_EQ(absent_matcher.error().code(), base::ErrorCode::kInvalidArgument);
    auto missing_row = manager.check_predicate_write_conflict(
        writer,
        base::RelationId{1},
        std::nullopt,
        std::nullopt
    );
    ASSERT_FALSE(missing_row);
    EXPECT_EQ(missing_row.error().code(), base::ErrorCode::kInvalidArgument);

    auto predicate = SsiPredicate::matcher(std::make_shared<FailingPredicateMatcher>());
    ASSERT_TRUE(predicate);
    ASSERT_TRUE(
        manager.record_predicate_read(reader, base::RelationId{1}, std::move(predicate).value())
    );
    const std::vector values{types::Value::integer(1)};
    auto status = manager.check_predicate_write_conflict(
        writer,
        base::RelationId{1},
        std::nullopt,
        std::span<const types::Value>{values}
    );
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code(), base::ErrorCode::kExecution);
}

TEST(SerializableConflictManagerTest, ExactRwCycleDoomsTheEdgeFormingActor) {
    SerializableConflictManager manager;
    constexpr base::Xid first{100};
    constexpr base::Xid second{200};
    const auto overlap = snapshot(100, 201, {first, second});
    begin_serializable(manager, first, overlap);
    begin_serializable(manager, second, overlap);

    const auto first_target = tuple_target(1, 0, 1);
    const auto second_target = tuple_target(1, 0, 2);
    ASSERT_TRUE(manager.record_read(second, read_record(second_target)));
    ASSERT_TRUE(manager.check_write_conflict(first, second_target));
    ASSERT_TRUE(manager.record_read(first, read_record(first_target)));
    ASSERT_TRUE(manager.check_write_conflict(second, first_target));

    EXPECT_TRUE(manager.is_doomed(second));
    auto rejected = manager.precommit(second);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kSerializationFailure);
    EXPECT_TRUE(rejected.error().is_retryable());
}

TEST(SerializableConflictManagerTest, TailPrecommitDoomsAnActiveDangerousPivot) {
    SerializableConflictManager manager;
    constexpr base::Xid head{100};
    constexpr base::Xid pivot{200};
    constexpr base::Xid tail{300};
    const auto overlap = snapshot(100, 301, {head, pivot, tail});
    for (const auto xid : {head, pivot, tail}) {
        begin_serializable(manager, xid, overlap);
    }

    const auto head_target = tuple_target(1, 0, 1);
    const auto pivot_target = tuple_target(1, 0, 2);
    ASSERT_TRUE(manager.record_read(pivot, read_record(pivot_target)));
    ASSERT_TRUE(manager.check_write_conflict(tail, pivot_target));
    ASSERT_TRUE(manager.record_read(head, read_record(head_target)));
    ASSERT_TRUE(manager.check_write_conflict(pivot, head_target));

    EXPECT_TRUE(manager.precommit(tail));
    EXPECT_TRUE(manager.is_doomed(pivot));
    auto pivot_commit = manager.precommit(pivot);
    ASSERT_FALSE(pivot_commit);
    EXPECT_EQ(pivot_commit.error().code(), base::ErrorCode::kSerializationFailure);
}

TEST(SerializableConflictManagerTest, PreparedPivotDoomsReaderDiscoveringIncomingEdge) {
    SerializableConflictManager manager;
    constexpr base::Xid reader{100};
    constexpr base::Xid pivot{200};
    constexpr base::Xid outgoing{300};
    const auto overlap = snapshot(100, 301, {reader, pivot, outgoing});
    for (const auto xid : {reader, pivot, outgoing}) {
        begin_serializable(manager, xid, overlap);
    }

    const auto target = tuple_target(1, 0, 2);
    ASSERT_TRUE(manager.record_read(pivot, read_record(target)));
    ASSERT_TRUE(manager.check_write_conflict(outgoing, target));
    ASSERT_TRUE(manager.mark_committed(outgoing));
    ASSERT_TRUE(manager.precommit(pivot));
    ASSERT_TRUE(manager.record_invisible_conflict(reader, pivot));

    EXPECT_TRUE(manager.is_doomed(reader));
    EXPECT_FALSE(manager.is_doomed(pivot));
}

TEST(SerializableConflictManagerTest, CommittedLocksRespectMvccOverlapAndPruneHorizon) {
    SerializableConflictManager manager;
    constexpr base::Xid old_snapshot{50};
    constexpr base::Xid reader{100};
    constexpr base::Xid newer_writer{200};
    constexpr base::Xid overlapping_writer{300};
    begin_serializable(manager, old_snapshot, snapshot(50, 51));
    begin_serializable(manager, reader, snapshot(50, 101, {old_snapshot}));
    const auto target = tuple_target(1, 0, 1);
    ASSERT_TRUE(manager.record_read(reader, read_record(target)));
    ASSERT_TRUE(manager.mark_committed(reader));

    manager.prune_committed();
    EXPECT_EQ(manager.read_lock_count(reader), 1U);
    begin_serializable(manager, newer_writer, snapshot(200, 201));
    ASSERT_TRUE(manager.check_write_conflict(newer_writer, target));
    EXPECT_TRUE(manager.outgoing_conflicts(reader).empty());

    begin_serializable(manager, overlapping_writer, snapshot(100, 301, {reader}));
    ASSERT_TRUE(manager.check_write_conflict(overlapping_writer, target));
    EXPECT_TRUE(contains(manager.outgoing_conflicts(reader), overlapping_writer));

    ASSERT_TRUE(manager.abort(old_snapshot));
    EXPECT_EQ(manager.read_lock_count(reader), 0U);
}

TEST(SerializableConflictManagerTest, DoomedOldestSnapshotAdvancesCommittedPruneHorizon) {
    SerializableConflictManager manager;
    constexpr base::Xid oldest{50};
    constexpr base::Xid committed_reader{100};
    constexpr base::Xid newer_active{200};
    begin_serializable(manager, oldest, snapshot(50, 51));
    begin_serializable(manager, committed_reader, snapshot(50, 101, {oldest}));
    const auto target = tuple_target(1, 0, 1);
    ASSERT_TRUE(manager.record_read(committed_reader, read_record(target)));
    ASSERT_TRUE(manager.mark_committed(committed_reader));

    begin_serializable(manager, newer_active, snapshot(200, 201));
    ASSERT_TRUE(manager.mark_doomed(oldest));
    manager.prune_committed();

    EXPECT_EQ(manager.commit_sequence(committed_reader), 0U);
    EXPECT_EQ(manager.target_count(), 0U);
    EXPECT_TRUE(manager.is_doomed(oldest));
    EXPECT_EQ(manager.snapshot_sequence(newer_active), 3U);
}

TEST(SerializableConflictManagerTest, ReleasedOutgoingEndpointLeavesConflictSummary) {
    SerializableConflictManager manager;
    constexpr base::Xid incoming{100};
    constexpr base::Xid pivot{200};
    constexpr base::Xid outgoing{300};
    begin_serializable(manager, pivot, snapshot(200, 301, {outgoing}));
    begin_serializable(manager, outgoing, snapshot(200, 301, {pivot}));
    const auto target = tuple_target(1, 0, 1);
    ASSERT_TRUE(manager.record_read(pivot, read_record(target)));
    ASSERT_TRUE(manager.check_write_conflict(outgoing, target));
    ASSERT_TRUE(manager.mark_committed(outgoing));

    begin_serializable(manager, incoming, snapshot(100, 301, {pivot}));
    ASSERT_TRUE(manager.mark_committed(pivot));
    manager.prune_committed();
    EXPECT_EQ(manager.transaction_count(), 2U);
    EXPECT_EQ(manager.commit_sequence(outgoing), 0U);
    EXPECT_NE(manager.commit_sequence(pivot), 0U);

    ASSERT_TRUE(manager.record_invisible_conflict(incoming, pivot));
    EXPECT_TRUE(manager.is_doomed(incoming));
}

TEST(SerializableConflictManagerTest, CreatorCleanupNeverOwnsAnotherReadersLock) {
    SerializableConflictManager manager;
    constexpr base::Xid creator{100};
    constexpr base::Xid reader{200};
    constexpr base::Xid writer{300};
    const auto overlap = snapshot(100, 301, {creator, reader, writer});
    for (const auto xid : {creator, reader, writer}) {
        begin_serializable(manager, xid, overlap);
    }
    const auto target = tuple_target(1, 4, 2);
    ASSERT_TRUE(manager.record_read(
        reader,
        SsiReadRecord{.target = target, .creator_xid = creator, .conflict_xid = base::kInvalidXid}
    ));
    ASSERT_TRUE(manager.abort(creator));
    EXPECT_EQ(manager.read_lock_count(reader), 1U);
    ASSERT_TRUE(manager.check_write_conflict(writer, target));
    EXPECT_TRUE(contains(manager.outgoing_conflicts(reader), writer));
}

TEST(SerializableConflictManagerTest, ConcurrentRepeatedReadsAndWritesRemainDeduplicated) {
    SerializableConflictManager manager;
    constexpr std::size_t pair_count = 4;
    const auto overlap = snapshot(
        100,
        108,
        {base::Xid{100},
         base::Xid{101},
         base::Xid{102},
         base::Xid{103},
         base::Xid{104},
         base::Xid{105},
         base::Xid{106},
         base::Xid{107}}
    );
    for (std::uint32_t value = 100; value < 108; ++value) {
        begin_serializable(manager, base::Xid{value}, overlap);
    }
    for (std::size_t pair = 0; pair < pair_count; ++pair) {
        ASSERT_TRUE(manager.record_read(
            base::Xid{static_cast<std::uint32_t>(100 + pair * 2)},
            read_record(tuple_target(static_cast<std::uint32_t>(pair + 1), 0, 1))
        ));
    }

    std::atomic_bool start{false};
    std::atomic_bool failed{false};
    std::vector<std::thread> workers;
    workers.reserve(pair_count * 2U);
    for (std::size_t index = 0; index < pair_count * 2; ++index) {
        workers.emplace_back([&, index] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            const auto pair = index / 2;
            const auto xid = base::Xid{static_cast<std::uint32_t>(100 + index)};
            const auto target = tuple_target(static_cast<std::uint32_t>(pair + 1), 0, 1);
            for (std::size_t iteration = 0; iteration < 100; ++iteration) {
                const auto status = index % 2 == 0 ? manager.record_read(xid, read_record(target))
                                                   : manager.check_write_conflict(xid, target);
                if (!status) {
                    failed.store(true, std::memory_order_release);
                    return;
                }
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) {
        worker.join();
    }

    EXPECT_FALSE(failed.load(std::memory_order_acquire));
    for (std::size_t pair = 0; pair < pair_count; ++pair) {
        const auto reader = base::Xid{static_cast<std::uint32_t>(100 + pair * 2)};
        const auto writer = base::Xid{static_cast<std::uint32_t>(101 + pair * 2)};
        EXPECT_EQ(manager.outgoing_conflicts(reader), std::vector<base::Xid>({writer}));
        EXPECT_FALSE(manager.is_doomed(reader));
        EXPECT_FALSE(manager.is_doomed(writer));
    }
}

TEST(SerializableConflictManagerTest, DoomedOperationsDeferRetryableFailureToPrecommit) {
    SerializableConflictManager manager;
    constexpr base::Xid xid{100};
    begin_serializable(manager, xid, snapshot(100, 101));
    ASSERT_TRUE(manager.mark_doomed(xid));
    ASSERT_TRUE(manager.record_read(xid, read_record(tuple_target(1, 0, 1))));
    ASSERT_TRUE(manager.check_write_conflict(xid, tuple_target(1, 0, 1)));
    EXPECT_EQ(manager.read_lock_count(xid), 0U);

    auto precommit = manager.precommit(xid);
    ASSERT_FALSE(precommit);
    EXPECT_EQ(precommit.error().code(), base::ErrorCode::kSerializationFailure);
    EXPECT_TRUE(precommit.error().is_retryable());
    ASSERT_TRUE(manager.abort(xid));
    EXPECT_EQ(manager.transaction_count(), 0U);
    EXPECT_TRUE(manager.abort(xid));
}

TEST(TransactionManagerTest, OwnsOneProcessWideSerializableConflictManager) {
    TransactionManager manager;
    auto* first = &manager.ssi_manager();
    auto* second = &manager.ssi_manager();
    EXPECT_EQ(first, second);
    constexpr base::Xid xid{100};
    begin_serializable(*first, xid, snapshot(100, 101));
    EXPECT_EQ(second->transaction_count(), 1U);
    ASSERT_TRUE(second->abort(xid));
}

}  // namespace
}  // namespace db::transaction
