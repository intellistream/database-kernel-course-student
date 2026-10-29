#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/manager.hpp"
#include "db/transaction/state.hpp"
#include "db/transaction/visibility.hpp"

namespace db::transaction {
namespace {

void set_status(CommitLog& commit_log, base::Xid xid, TransactionStatus status) {
    ASSERT_TRUE(commit_log.ensure_xid_allocated(xid));
    ASSERT_TRUE(commit_log.set_status(xid, status));
}

auto register_session(TransactionManager& manager) -> ProcSlotLease {
    auto registered = manager.register_session();
    EXPECT_TRUE(registered);
    return std::move(*registered);
}

auto assign_xid(TransactionManager& manager, ProcSlotLease& lease) -> base::Xid {
    auto assigned = manager.assign_xid(lease);
    EXPECT_TRUE(assigned);
    return *assigned;
}

TEST(VisibilityTest, ClassifiesSnapshotBoundariesBeforeMembershipSearch) {
    const Snapshot snapshot{
        .xmin = base::Xid{10},
        .xmax = base::Xid{20},
        .in_progress = {base::Xid{12}, base::Xid{15}},
        .command_id = 3
    };

    EXPECT_FALSE(xid_in_mvcc_snapshot(base::kInvalidXid, snapshot));
    EXPECT_FALSE(xid_in_mvcc_snapshot(base::Xid{9}, snapshot));
    EXPECT_FALSE(xid_in_mvcc_snapshot(base::Xid{10}, snapshot));
    EXPECT_TRUE(xid_in_mvcc_snapshot(base::Xid{12}, snapshot));
    EXPECT_FALSE(xid_in_mvcc_snapshot(base::Xid{19}, snapshot));
    EXPECT_TRUE(xid_in_mvcc_snapshot(base::Xid{20}, snapshot));
    EXPECT_TRUE(xid_in_mvcc_snapshot(base::Xid{21}, snapshot));
    EXPECT_FALSE(xid_in_mvcc_snapshot(kXidWrapBoundary, snapshot));
}

TEST(VisibilityTest, AppliesCreatorStatusSnapshotAndCommandRulesInRequiredOrder) {
    CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{6}, TransactionStatus::kAborted);
    set_status(commit_log, base::Xid{8}, TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{12}, TransactionStatus::kCommitted);
    const Snapshot snapshot{
        .xmin = base::Xid{5},
        .xmax = base::Xid{10},
        .in_progress = {base::Xid{7}, base::Xid{8}},
        .command_id = 2
    };
    constexpr base::Xid own_xid{9};

    EXPECT_TRUE(tuple_visible_in_snapshot(
        TupleVersionState{.xmin = base::kInvalidXid},
        own_xid,
        snapshot,
        commit_log
    ));
    EXPECT_TRUE(tuple_visible_in_snapshot(
        TupleVersionState{.xmin = base::Xid{5}},
        own_xid,
        snapshot,
        commit_log
    ));
    EXPECT_FALSE(tuple_visible_in_snapshot(
        TupleVersionState{.xmin = base::Xid{6}},
        own_xid,
        snapshot,
        commit_log
    ));
    EXPECT_FALSE(tuple_visible_in_snapshot(
        TupleVersionState{.xmin = base::Xid{7}},
        own_xid,
        snapshot,
        commit_log
    ));
    // XID 8 is committed now, but was a member at capture time.
    EXPECT_FALSE(tuple_visible_in_snapshot(
        TupleVersionState{.xmin = base::Xid{8}},
        own_xid,
        snapshot,
        commit_log
    ));
    // XID 12 is committed now, but lies beyond the fixed xmax.
    EXPECT_FALSE(tuple_visible_in_snapshot(
        TupleVersionState{.xmin = base::Xid{12}},
        own_xid,
        snapshot,
        commit_log
    ));
    EXPECT_TRUE(tuple_visible_in_snapshot(
        TupleVersionState{.xmin = own_xid, .command_id = 1},
        own_xid,
        snapshot,
        commit_log
    ));
    EXPECT_FALSE(tuple_visible_in_snapshot(
        TupleVersionState{.xmin = own_xid, .command_id = 2},
        own_xid,
        snapshot,
        commit_log
    ));
}

TEST(VisibilityTest, AppliesModifierStatusSnapshotLockAndCommandRules) {
    CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{6}, TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{7}, TransactionStatus::kAborted);
    set_status(commit_log, base::Xid{8}, TransactionStatus::kCommitted);
    const Snapshot snapshot{
        .xmin = base::Xid{5},
        .xmax = base::Xid{10},
        .in_progress = {base::Xid{8}},
        .command_id = 2
    };
    constexpr base::Xid own_xid{9};
    const auto base_tuple = TupleVersionState{.xmin = base::Xid{5}};

    EXPECT_TRUE(tuple_visible_in_snapshot(base_tuple, own_xid, snapshot, commit_log));
    auto lock_only = base_tuple;
    lock_only.xmax = base::Xid{6};
    lock_only.xmax_lock_only = true;
    EXPECT_TRUE(tuple_visible_in_snapshot(lock_only, own_xid, snapshot, commit_log));

    auto own_old_delete = base_tuple;
    own_old_delete.xmax = own_xid;
    own_old_delete.command_id = 1;
    EXPECT_FALSE(tuple_visible_in_snapshot(own_old_delete, own_xid, snapshot, commit_log));
    own_old_delete.command_id = 2;
    EXPECT_TRUE(tuple_visible_in_snapshot(own_old_delete, own_xid, snapshot, commit_log));

    auto after_snapshot_delete = base_tuple;
    after_snapshot_delete.xmax = base::Xid{8};
    EXPECT_TRUE(tuple_visible_in_snapshot(after_snapshot_delete, own_xid, snapshot, commit_log));
    auto before_snapshot_delete = base_tuple;
    before_snapshot_delete.xmax = base::Xid{6};
    EXPECT_FALSE(tuple_visible_in_snapshot(before_snapshot_delete, own_xid, snapshot, commit_log));
    auto aborted_delete = base_tuple;
    aborted_delete.xmax = base::Xid{7};
    EXPECT_TRUE(tuple_visible_in_snapshot(aborted_delete, own_xid, snapshot, commit_log));
    auto active_delete = base_tuple;
    active_delete.xmax = base::Xid{10};
    EXPECT_TRUE(tuple_visible_in_snapshot(active_delete, own_xid, snapshot, commit_log));
}

TEST(VisibilityTest, DistinguishesEveryTupleModificationOutcome) {
    CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{6}, TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{7}, TransactionStatus::kAborted);
    const Snapshot
        snapshot{.xmin = base::Xid{5}, .xmax = base::Xid{12}, .in_progress = {}, .command_id = 3};
    constexpr base::Xid own_xid{9};
    const auto original = TupleVersionState{.xmin = base::Xid{5}};

    EXPECT_EQ(
        tuple_satisfies_update(original, own_xid, snapshot, commit_log),
        (TupleModificationDecision{.result = TupleModificationResult::kOk})
    );
    EXPECT_EQ(
        tuple_satisfies_update(
            TupleVersionState{.xmin = own_xid, .command_id = 3},
            own_xid,
            snapshot,
            commit_log
        ),
        (TupleModificationDecision{.result = TupleModificationResult::kInvisible})
    );

    auto self_modified = original;
    self_modified.xmax = own_xid;
    EXPECT_EQ(
        tuple_satisfies_update(self_modified, own_xid, snapshot, commit_log).result,
        TupleModificationResult::kSelfModified
    );
    auto aborted = original;
    aborted.xmax = base::Xid{7};
    EXPECT_EQ(
        tuple_satisfies_update(aborted, own_xid, snapshot, commit_log).result,
        TupleModificationResult::kOk
    );
    auto updated = original;
    updated.xmax = base::Xid{6};
    EXPECT_EQ(
        tuple_satisfies_update(updated, own_xid, snapshot, commit_log).result,
        TupleModificationResult::kUpdated
    );
    updated.xmax_deleted = true;
    EXPECT_EQ(
        tuple_satisfies_update(updated, own_xid, snapshot, commit_log).result,
        TupleModificationResult::kDeleted
    );

    auto active = original;
    active.xmax = base::Xid{10};
    EXPECT_EQ(
        tuple_satisfies_update(active, own_xid, snapshot, commit_log),
        (TupleModificationDecision{
            .result = TupleModificationResult::kBeingModified,
            .blocking_xid = base::Xid{10}
        })
    );
    active.xmax_lock_only = true;
    EXPECT_EQ(
        tuple_satisfies_update(active, own_xid, snapshot, commit_log).result,
        TupleModificationResult::kBeingModified
    );
    active.xmax = base::Xid{6};
    EXPECT_EQ(
        tuple_satisfies_update(active, own_xid, snapshot, commit_log).result,
        TupleModificationResult::kOk
    );
}

TEST(SnapshotCoordinatorTest, ReadCommittedRefreshesAfterEveryCompletionBoundary) {
    TransactionManager manager;
    auto reader = register_session(manager);
    auto writer = register_session(manager);
    const auto reader_xid = assign_xid(manager, reader);
    const auto writer_xid = assign_xid(manager, writer);
    auto coordinator = manager.snapshot_coordinator(reader);
    TransactionStateMachine state;
    ASSERT_TRUE(state.begin_explicit());

    auto first_command = state.start_command();
    ASSERT_TRUE(first_command);
    auto first = coordinator.acquire(*first_command);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->command_id, 1U);
    EXPECT_TRUE(xid_in_mvcc_snapshot(writer_xid, *first));
    ASSERT_TRUE(state.finish_command(CommandOutcome::kSucceeded));

    ASSERT_TRUE(manager.complete(writer, writer_xid, TransactionStatus::kCommitted));
    auto second_command = state.start_command();
    ASSERT_TRUE(second_command);
    auto second = coordinator.acquire(*second_command);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->command_id, 2U);
    EXPECT_GT(second->xmax.value(), first->xmax.value());
    EXPECT_FALSE(xid_in_mvcc_snapshot(writer_xid, *second));
    ASSERT_TRUE(state.finish_command(CommandOutcome::kSucceeded));
    ASSERT_TRUE(state.commit_explicit());

    ASSERT_TRUE(coordinator.release(reader_xid));
    ASSERT_TRUE(manager.complete(reader, reader_xid, TransactionStatus::kCommitted));
    ASSERT_TRUE(reader.close());
    ASSERT_TRUE(writer.close());
}

TEST(SnapshotCoordinatorTest, RepeatableReadReusesTransactionSetButAdvancesCommandId) {
    TransactionManager manager;
    auto reader = register_session(manager);
    auto writer = register_session(manager);
    const auto reader_xid = assign_xid(manager, reader);
    const auto writer_xid = assign_xid(manager, writer);
    auto coordinator = manager.snapshot_coordinator(reader);
    TransactionStateMachine state;
    ASSERT_TRUE(state.set_isolation(IsolationLevel::kRepeatableRead));
    auto started = state.begin_explicit();
    ASSERT_TRUE(started);
    auto fixed = coordinator.acquire(*started);
    ASSERT_TRUE(fixed);
    EXPECT_EQ(fixed->command_id, 0U);
    EXPECT_TRUE(coordinator.has_transaction_snapshot());

    auto first_command = state.start_command();
    ASSERT_TRUE(first_command);
    auto first = coordinator.acquire(*first_command);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->command_id, 1U);
    ASSERT_TRUE(state.finish_command(CommandOutcome::kSucceeded));
    ASSERT_TRUE(manager.complete(writer, writer_xid, TransactionStatus::kCommitted));

    auto second_command = state.start_command();
    ASSERT_TRUE(second_command);
    auto second = coordinator.acquire(*second_command);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->command_id, 2U);
    EXPECT_EQ(second->xmin, first->xmin);
    EXPECT_EQ(second->xmax, first->xmax);
    EXPECT_EQ(second->in_progress, first->in_progress);
    EXPECT_TRUE(xid_in_mvcc_snapshot(writer_xid, *second));
    ASSERT_TRUE(state.finish_command(CommandOutcome::kSucceeded));
    ASSERT_TRUE(state.rollback_explicit());

    ASSERT_TRUE(coordinator.release(reader_xid));
    ASSERT_TRUE(manager.complete(reader, reader_xid, TransactionStatus::kAborted));
    ASSERT_TRUE(reader.close());
    ASSERT_TRUE(writer.close());
}

TEST(SnapshotCoordinatorTest, RejectsMissingXidInvalidActionsAndStaleReuse) {
    TransactionManager manager;
    auto lease = register_session(manager);
    auto coordinator = manager.snapshot_coordinator(lease);
    TransactionStateMachine state;
    auto command = state.start_command();
    ASSERT_TRUE(command);
    auto without_xid = coordinator.acquire(*command);
    ASSERT_FALSE(without_xid);
    EXPECT_EQ(without_xid.error().code(), base::ErrorCode::kTransactionState);

    const auto xid = assign_xid(manager, lease);
    auto fresh = coordinator.acquire(*command);
    ASSERT_TRUE(fresh);
    const CommandStart stale_reuse{
        .transaction_epoch = command->transaction_epoch + 1U,
        .command_id = 2,
        .isolation = IsolationLevel::kRepeatableRead,
        .scope = TransactionScope::kExplicit,
        .snapshot_action = SnapshotAction::kReuseTransaction,
        .starts_transaction = false
    };
    auto stale = coordinator.acquire(stale_reuse);
    ASSERT_FALSE(stale);
    EXPECT_EQ(stale.error().code(), base::ErrorCode::kTransactionState);

    const TransactionStart no_snapshot{
        .epoch = command->transaction_epoch,
        .isolation = IsolationLevel::kReadCommitted,
        .snapshot_action = SnapshotAction::kNone
    };
    auto none = coordinator.acquire(no_snapshot);
    ASSERT_FALSE(none);
    EXPECT_EQ(none.error().code(), base::ErrorCode::kTransactionState);
    auto wrong_release = coordinator.release(base::Xid{xid.value() + 1U});
    ASSERT_FALSE(wrong_release);
    EXPECT_EQ(wrong_release.error().code(), base::ErrorCode::kTransactionState);

    ASSERT_TRUE(coordinator.release(xid));
    ASSERT_TRUE(manager.complete(lease, xid, TransactionStatus::kAborted));
    ASSERT_TRUE(lease.close());
    auto closed_lease = coordinator.acquire(*command);
    ASSERT_FALSE(closed_lease);
    EXPECT_EQ(closed_lease.error().code(), base::ErrorCode::kTransactionState);
}

}  // namespace
}  // namespace db::transaction
