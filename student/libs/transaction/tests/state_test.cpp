#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/transaction/state.hpp"

namespace db::transaction {
namespace {

void expect_transaction_error(const base::Error& error, std::string_view message) {
    EXPECT_EQ(error.code(), base::ErrorCode::kTransactionState);
    EXPECT_EQ(error.message(), message);
    ASSERT_EQ(error.contexts().size(), 1U);
    EXPECT_EQ(error.contexts().front().component, "transaction");
}

TEST(TransactionStateTest, ExposesStableIsolationAndBlockStateNames) {
    EXPECT_EQ(isolation_level_name(IsolationLevel::kReadCommitted), "read committed");
    EXPECT_EQ(isolation_level_name(IsolationLevel::kRepeatableRead), "repeatable read");
    EXPECT_EQ(isolation_level_name(IsolationLevel::kSerializable), "serializable");
    // Fixed-width unknown values exercise stable diagnostic fallbacks.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_EQ(isolation_level_name(static_cast<IsolationLevel>(255)), "unknown");

    EXPECT_EQ(transaction_block_state_name(TransactionBlockState::kIdle), "idle");
    EXPECT_EQ(
        transaction_block_state_name(TransactionBlockState::kInTransaction),
        "in transaction"
    );
    EXPECT_EQ(
        transaction_block_state_name(TransactionBlockState::kFailedTransaction),
        "failed transaction"
    );
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_EQ(transaction_block_state_name(static_cast<TransactionBlockState>(255)), "unknown");
}

TEST(TransactionStateTest, ImplicitCommandsAutocommitOrAbortAndReturnToIdle) {
    TransactionStateMachine state;

    auto first = state.start_command();
    ASSERT_TRUE(first);
    EXPECT_EQ(first->transaction_epoch, 1U);
    EXPECT_EQ(first->command_id, 1U);
    EXPECT_EQ(first->scope, TransactionScope::kImplicit);
    EXPECT_EQ(first->isolation, IsolationLevel::kReadCommitted);
    EXPECT_EQ(first->snapshot_action, SnapshotAction::kCaptureCommand);
    EXPECT_TRUE(first->starts_transaction);
    EXPECT_TRUE(state.command_active());

    auto committed = state.finish_command(CommandOutcome::kSucceeded);
    ASSERT_TRUE(committed);
    EXPECT_EQ(committed->disposition, TransactionDisposition::kCommit);
    EXPECT_EQ(committed->state, TransactionBlockState::kIdle);
    EXPECT_EQ(state.state(), TransactionBlockState::kIdle);
    EXPECT_EQ(state.scope(), TransactionScope::kNone);

    auto second = state.start_command();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->transaction_epoch, 2U);
    auto aborted = state.finish_command(CommandOutcome::kFailed);
    ASSERT_TRUE(aborted);
    EXPECT_EQ(aborted->disposition, TransactionDisposition::kAbort);
    EXPECT_EQ(state.state(), TransactionBlockState::kIdle);
}

TEST(TransactionStateTest, ExplicitCommandsAdvanceAndCommitOnlyAtBlockEnd) {
    TransactionStateMachine state;

    auto started = state.begin_explicit();
    ASSERT_TRUE(started);
    EXPECT_EQ(started->epoch, 1U);
    EXPECT_EQ(started->isolation, IsolationLevel::kReadCommitted);
    EXPECT_EQ(started->snapshot_action, SnapshotAction::kNone);

    auto first = state.start_command();
    ASSERT_TRUE(first);
    EXPECT_FALSE(first->starts_transaction);
    EXPECT_EQ(first->command_id, 1U);
    EXPECT_EQ(first->scope, TransactionScope::kExplicit);
    EXPECT_EQ(first->snapshot_action, SnapshotAction::kCaptureCommand);
    auto first_done = state.finish_command(CommandOutcome::kSucceeded);
    ASSERT_TRUE(first_done);
    EXPECT_EQ(first_done->disposition, TransactionDisposition::kNone);
    EXPECT_EQ(first_done->state, TransactionBlockState::kInTransaction);

    auto second = state.start_command();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->command_id, 2U);
    auto second_done = state.finish_command(CommandOutcome::kSucceeded);
    ASSERT_TRUE(second_done);

    auto committed = state.commit_explicit();
    ASSERT_TRUE(committed);
    EXPECT_EQ(*committed, TransactionDisposition::kCommit);
    EXPECT_EQ(state.state(), TransactionBlockState::kIdle);
    EXPECT_EQ(state.command_id(), 0U);
}

TEST(TransactionStateTest, ExplicitFailurePoisonsBlockUntilRollback) {
    TransactionStateMachine state;
    ASSERT_TRUE(state.begin_explicit());
    ASSERT_TRUE(state.start_command());

    auto failed = state.finish_command(CommandOutcome::kFailed);
    ASSERT_TRUE(failed);
    EXPECT_EQ(failed->state, TransactionBlockState::kFailedTransaction);
    EXPECT_EQ(failed->disposition, TransactionDisposition::kNone);

    auto command = state.start_command();
    ASSERT_FALSE(command);
    expect_transaction_error(
        command.error(),
        "current transaction is aborted; commands are ignored until rollback"
    );
    auto commit = state.commit_explicit();
    ASSERT_FALSE(commit);
    expect_transaction_error(
        commit.error(),
        "current transaction is aborted; commands are ignored until rollback"
    );
    auto isolation = state.set_isolation(IsolationLevel::kSerializable);
    ASSERT_FALSE(isolation);
    expect_transaction_error(
        isolation.error(),
        "current transaction is aborted; commands are ignored until rollback"
    );

    auto rolled_back = state.rollback_explicit();
    ASSERT_TRUE(rolled_back);
    EXPECT_EQ(*rolled_back, TransactionDisposition::kAbort);
    EXPECT_EQ(state.state(), TransactionBlockState::kIdle);
    ASSERT_TRUE(state.start_command());
}

TEST(TransactionStateTest, IdleIsolationPersistsAndFixedSnapshotsStartAtBegin) {
    TransactionStateMachine state;
    ASSERT_TRUE(state.set_isolation(IsolationLevel::kRepeatableRead));
    EXPECT_EQ(state.default_isolation(), IsolationLevel::kRepeatableRead);

    auto started = state.begin_explicit();
    ASSERT_TRUE(started);
    EXPECT_EQ(started->snapshot_action, SnapshotAction::kCaptureTransaction);
    EXPECT_TRUE(state.snapshot_fixed());

    auto command = state.start_command();
    ASSERT_TRUE(command);
    EXPECT_EQ(command->snapshot_action, SnapshotAction::kReuseTransaction);
    ASSERT_TRUE(state.finish_command(CommandOutcome::kSucceeded));

    auto isolation = state.set_isolation(IsolationLevel::kSerializable);
    ASSERT_FALSE(isolation);
    expect_transaction_error(
        isolation.error(),
        "transaction isolation cannot change after the first data snapshot"
    );
    ASSERT_TRUE(state.rollback_explicit());

    auto implicit = state.start_command();
    ASSERT_TRUE(implicit);
    EXPECT_EQ(implicit->isolation, IsolationLevel::kRepeatableRead);
    EXPECT_EQ(implicit->snapshot_action, SnapshotAction::kCaptureTransaction);
}

TEST(TransactionStateTest, ActiveIsolationBeforeFirstCommandIsTransactionLocal) {
    TransactionStateMachine state;
    ASSERT_TRUE(state.begin_explicit());
    ASSERT_TRUE(state.set_isolation(IsolationLevel::kSerializable));
    EXPECT_EQ(state.isolation(), IsolationLevel::kSerializable);
    EXPECT_EQ(state.default_isolation(), IsolationLevel::kReadCommitted);

    auto first = state.start_command();
    ASSERT_TRUE(first);
    EXPECT_EQ(first->snapshot_action, SnapshotAction::kCaptureTransaction);
    EXPECT_TRUE(state.snapshot_fixed());
    ASSERT_TRUE(state.finish_command(CommandOutcome::kSucceeded));

    auto late_change = state.set_isolation(IsolationLevel::kReadCommitted);
    ASSERT_FALSE(late_change);
    ASSERT_TRUE(state.commit_explicit());

    auto next = state.begin_explicit();
    ASSERT_TRUE(next);
    EXPECT_EQ(next->isolation, IsolationLevel::kReadCommitted);
    EXPECT_EQ(next->snapshot_action, SnapshotAction::kNone);
}

TEST(TransactionStateTest, ReadCommittedCapturesAFreshSnapshotForEveryCommand) {
    TransactionStateMachine state;
    ASSERT_TRUE(state.begin_explicit());

    auto first = state.start_command();
    ASSERT_TRUE(first);
    EXPECT_EQ(first->snapshot_action, SnapshotAction::kCaptureCommand);
    ASSERT_TRUE(state.finish_command(CommandOutcome::kSucceeded));

    auto second = state.start_command();
    ASSERT_TRUE(second);
    EXPECT_EQ(second->snapshot_action, SnapshotAction::kCaptureCommand);
    ASSERT_TRUE(state.finish_command(CommandOutcome::kSucceeded));
    ASSERT_TRUE(state.commit_explicit());
}

TEST(TransactionStateTest, RejectsNestedAndMismatchedLifecycleOperations) {
    TransactionStateMachine state;
    auto commit_idle = state.commit_explicit();
    ASSERT_FALSE(commit_idle);
    expect_transaction_error(commit_idle.error(), "no transaction is in progress");
    auto rollback_idle = state.rollback_explicit();
    ASSERT_FALSE(rollback_idle);
    expect_transaction_error(rollback_idle.error(), "no transaction is in progress");
    auto finish_idle = state.finish_command(CommandOutcome::kSucceeded);
    ASSERT_FALSE(finish_idle);
    expect_transaction_error(finish_idle.error(), "no data command is active");

    ASSERT_TRUE(state.begin_explicit());
    auto nested_begin = state.begin_explicit();
    ASSERT_FALSE(nested_begin);
    expect_transaction_error(nested_begin.error(), "transaction already in progress");
    ASSERT_TRUE(state.start_command());
    auto nested_command = state.start_command();
    ASSERT_FALSE(nested_command);
    expect_transaction_error(nested_command.error(), "another command is already active");
    auto commit_active = state.commit_explicit();
    ASSERT_FALSE(commit_active);
    expect_transaction_error(commit_active.error(), "a data command is still active");
    auto rollback_active = state.rollback_explicit();
    ASSERT_FALSE(rollback_active);
    expect_transaction_error(rollback_active.error(), "a data command is still active");
    ASSERT_TRUE(state.finish_command(CommandOutcome::kSucceeded));
    ASSERT_TRUE(state.rollback_explicit());
}

}  // namespace
}  // namespace db::transaction
