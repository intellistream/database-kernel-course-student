#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/transaction/backend.hpp"
#include "db/transaction/lock_manager.hpp"
#include "db/transaction/manager.hpp"

#include "completion_journal_test_support.hpp"

namespace db::transaction {
namespace {

auto create_backend(TransactionManager& manager, TransactionBackendOptions options = {})
    -> std::unique_ptr<TransactionBackend> {
    auto backend = TransactionBackend::create(manager, options);
    EXPECT_TRUE(backend);
    return std::move(*backend);
}

TEST(TransactionBackendTest, CoordinatesStateSnapshotsAndStableSlotAcrossTransactions) {
    TransactionManager manager;
    auto backend =
        create_backend(manager, TransactionBackendOptions{.track_persistent_writes = true});
    const auto* stable_slot = backend->proc_slot();
    ASSERT_NE(stable_slot, nullptr);

    auto explicit_start = backend->begin_explicit();
    ASSERT_TRUE(explicit_start);
    EXPECT_EQ(explicit_start->xid, base::Xid{1});
    EXPECT_FALSE(explicit_start->snapshot);
    auto command = backend->start_command();
    ASSERT_TRUE(command);
    EXPECT_EQ(command->snapshot.command_id, 1U);
    auto command_finish = backend->finish_command(CommandOutcome::kSucceeded);
    ASSERT_TRUE(command_finish);
    EXPECT_EQ(command_finish->disposition, TransactionDisposition::kNone);
    ASSERT_TRUE(backend->commit_explicit());
    EXPECT_TRUE(manager.commit_log().is_committed(base::Xid{1}));
    EXPECT_EQ(backend->xid(), base::kInvalidXid);
    EXPECT_EQ(backend->proc_slot(), stable_slot);

    auto implicit = backend->start_command();
    ASSERT_TRUE(implicit);
    EXPECT_TRUE(implicit->transition.starts_transaction);
    EXPECT_EQ(implicit->xid, base::Xid{2});
    auto aborted = backend->finish_command(CommandOutcome::kFailed);
    ASSERT_TRUE(aborted);
    EXPECT_EQ(aborted->disposition, TransactionDisposition::kAbort);
    EXPECT_TRUE(manager.commit_log().is_aborted(base::Xid{2}));
    EXPECT_EQ(backend->state(), TransactionBlockState::kIdle);
    EXPECT_EQ(backend->proc_slot(), stable_slot);
    ASSERT_TRUE(backend->close());
}

TEST(BackendLocalStateTest, TargetsAreBackendLocalAndClearConditionally) {
    BackendLocalState first;
    BackendLocalState second;

    ASSERT_TRUE(first.set_heap_target(base::RelationId{7}, base::PageId{11}));
    ASSERT_TRUE(second.set_heap_target(base::RelationId{7}, base::PageId{12}));
    EXPECT_EQ(first.heap_target_count(), 1U);
    EXPECT_EQ(second.heap_target_count(), 1U);

    ASSERT_TRUE(first.clear_heap_target(base::RelationId{7}, base::PageId{12}));
    EXPECT_EQ(first.heap_target(base::RelationId{7}), base::PageId{11});
    EXPECT_EQ(second.heap_target(base::RelationId{7}), base::PageId{12});

    ASSERT_TRUE(first.clear_heap_target(base::RelationId{7}, base::PageId{11}));
    EXPECT_FALSE(first.heap_target(base::RelationId{7}));
    EXPECT_EQ(first.heap_target_count(), 0U);
}

TEST(BackendLocalStateTest, RejectsInvalidTargetsAndLivesAcrossTransactions) {
    BackendLocalState state;
    EXPECT_FALSE(state.set_heap_target(base::kInvalidRelationId, base::PageId{1}));
    EXPECT_FALSE(state.set_heap_target(base::RelationId{1}, base::kInvalidPageId));
    EXPECT_FALSE(state.clear_heap_target(base::kInvalidRelationId, base::PageId{1}));
    EXPECT_FALSE(state.clear_heap_target(base::RelationId{1}, base::kInvalidPageId));

    TransactionManager manager;
    auto backend = create_backend(manager);
    ASSERT_TRUE(backend->local_state().set_heap_target(base::RelationId{9}, base::PageId{3}));
    ASSERT_TRUE(backend->start_command());
    ASSERT_TRUE(backend->finish_command(CommandOutcome::kSucceeded));
    EXPECT_EQ(backend->local_state().heap_target(base::RelationId{9}), base::PageId{3});
    ASSERT_TRUE(backend->close());
}

TEST(TransactionBackendTest, RejectsMismatchedLifecycleAndDuplicateCloseOperations) {
    TransactionManager manager;
    auto backend = create_backend(manager);
    EXPECT_EQ(backend->scope(), TransactionScope::kNone);
    EXPECT_EQ(backend->isolation(), IsolationLevel::kReadCommitted);
    EXPECT_FALSE(backend->mark_persistent_write());
    EXPECT_FALSE(backend->finish_command(CommandOutcome::kSucceeded));
    EXPECT_FALSE(backend->commit_explicit());
    EXPECT_FALSE(backend->rollback_explicit());

    ASSERT_TRUE(backend->begin_explicit());
    EXPECT_EQ(backend->scope(), TransactionScope::kExplicit);
    EXPECT_FALSE(backend->begin_explicit());
    ASSERT_TRUE(backend->start_command());
    EXPECT_FALSE(backend->start_command());
    EXPECT_FALSE(backend->commit_explicit());
    EXPECT_FALSE(backend->rollback_explicit());
    ASSERT_TRUE(backend->finish_command(CommandOutcome::kFailed));
    EXPECT_FALSE(backend->start_command());
    EXPECT_FALSE(backend->commit_explicit());
    EXPECT_FALSE(backend->set_isolation(IsolationLevel::kRepeatableRead));
    ASSERT_TRUE(backend->rollback_explicit());

    ASSERT_TRUE(backend->close());
    EXPECT_EQ(backend->proc_slot(), nullptr);
    auto duplicate_close = backend->close();
    ASSERT_FALSE(duplicate_close);
    EXPECT_EQ(duplicate_close.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(TransactionBackendTest, ExplicitFailureRetainsResourcesUntilRollback) {
    TransactionManager manager;
    auto backend = create_backend(manager);
    auto started = backend->begin_explicit();
    ASSERT_TRUE(started);
    const auto xid = started->xid;
    ASSERT_TRUE(manager.lock_manager()
                    .acquire_table(xid, base::RelationId{9}, TableLockMode::kAccessExclusive));
    ASSERT_TRUE(backend->start_command());
    auto failed = backend->finish_command(CommandOutcome::kFailed);
    ASSERT_TRUE(failed);
    EXPECT_EQ(failed->state, TransactionBlockState::kFailedTransaction);
    EXPECT_EQ(backend->proc_slot()->xid(), xid);
    EXPECT_EQ(
        manager.lock_manager().held_table_mode(xid, base::RelationId{9}),
        TableLockMode::kAccessExclusive
    );

    ASSERT_TRUE(backend->rollback_explicit());
    EXPECT_TRUE(manager.commit_log().is_aborted(xid));
    EXPECT_FALSE(backend->proc_slot()->has_active_transaction());
    EXPECT_FALSE(manager.lock_manager().held_table_mode(xid, base::RelationId{9}));
    ASSERT_TRUE(backend->close());
}

TEST(TransactionBackendTest, ProvenReadOnlyCommitSkipsJournalAndWriteTrackingResets) {
    auto completion_journal = std::make_unique<test::RecordingCompletionJournal>();
    auto* journal_view = completion_journal.get();
    auto manager_result =
        TransactionManager::with_completion_journal(std::move(completion_journal));
    ASSERT_TRUE(manager_result);
    auto backend = create_backend(
        **manager_result,
        TransactionBackendOptions{.track_persistent_writes = true}
    );

    auto read_only = backend->start_command();
    ASSERT_TRUE(read_only);
    const auto read_only_xid = read_only->xid;
    ASSERT_TRUE(backend->finish_command(CommandOutcome::kSucceeded));
    EXPECT_TRUE((*manager_result)->commit_log().is_committed(read_only_xid));
    EXPECT_EQ(journal_view->append_count(), 0U);
    EXPECT_EQ(journal_view->flush_count(), 0U);

    auto writer = backend->start_command();
    ASSERT_TRUE(writer);
    const auto writer_xid = writer->xid;
    EXPECT_FALSE(backend->has_persistent_writes());
    ASSERT_TRUE(backend->mark_persistent_write());
    EXPECT_TRUE(backend->has_persistent_writes());
    ASSERT_TRUE(backend->finish_command(CommandOutcome::kSucceeded));
    EXPECT_TRUE((*manager_result)->commit_log().is_committed(writer_xid));
    EXPECT_EQ(journal_view->append_count(), 1U);
    EXPECT_EQ(journal_view->flush_count(), 1U);

    ASSERT_TRUE(backend->start_command());
    EXPECT_FALSE(backend->has_persistent_writes());
    ASSERT_TRUE(backend->finish_command(CommandOutcome::kSucceeded));
    ASSERT_TRUE(backend->close());
}

TEST(TransactionBackendTest, FailedJournalFlushAutomaticallyRollsBackAndReleasesLocks) {
    auto completion_journal = std::make_unique<test::RecordingCompletionJournal>();
    auto* journal_view = completion_journal.get();
    auto manager_result =
        TransactionManager::with_completion_journal(std::move(completion_journal));
    ASSERT_TRUE(manager_result);
    auto backend = create_backend(**manager_result);
    auto command = backend->start_command();
    ASSERT_TRUE(command);
    const auto xid = command->xid;
    ASSERT_TRUE((*manager_result)
                    ->lock_manager()
                    .acquire_table(xid, base::RelationId{11}, TableLockMode::kAccessExclusive));

    journal_view->fail_next_flush();
    auto completion = backend->finish_command(CommandOutcome::kSucceeded);
    ASSERT_FALSE(completion);
    EXPECT_EQ(completion.error().code(), base::ErrorCode::kIo);
    EXPECT_TRUE((*manager_result)->commit_log().is_aborted(xid));
    EXPECT_EQ(backend->xid(), base::kInvalidXid);
    EXPECT_FALSE(backend->proc_slot()->has_active_transaction());
    EXPECT_FALSE((*manager_result)->lock_manager().held_table_mode(xid, base::RelationId{11}));
    const auto appends = journal_view->appends();
    ASSERT_EQ(appends.size(), 2U);
    EXPECT_EQ(appends[0].outcome, CompletionOutcome::kCommit);
    EXPECT_EQ(appends[1].outcome, CompletionOutcome::kAbort);
    EXPECT_EQ(journal_view->flush_count(), 1U);
    ASSERT_TRUE(backend->close());
}

TEST(TransactionBackendTest, FailedImplicitAbortRetainsCommandForExplicitRetry) {
    auto completion_journal = std::make_unique<test::RecordingCompletionJournal>();
    auto* journal_view = completion_journal.get();
    auto manager_result =
        TransactionManager::with_completion_journal(std::move(completion_journal));
    ASSERT_TRUE(manager_result);
    auto backend = create_backend(**manager_result);
    auto command = backend->start_command();
    ASSERT_TRUE(command);
    const auto xid = command->xid;
    ASSERT_TRUE((*manager_result)
                    ->lock_manager()
                    .acquire_table(xid, base::RelationId{12}, TableLockMode::kAccessExclusive));

    journal_view->fail_next_append(CompletionOutcome::kAbort);
    auto failed = backend->finish_command(CommandOutcome::kFailed);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_TRUE(backend->command_active());
    EXPECT_EQ(backend->state(), TransactionBlockState::kInTransaction);
    EXPECT_EQ(backend->scope(), TransactionScope::kImplicit);
    EXPECT_EQ(backend->xid(), xid);
    EXPECT_TRUE((*manager_result)->commit_log().is_in_progress(xid));
    ASSERT_NE(backend->proc_slot(), nullptr);
    EXPECT_EQ(backend->proc_slot()->xid(), xid);
    EXPECT_EQ(
        (*manager_result)->lock_manager().held_table_mode(xid, base::RelationId{12}),
        TableLockMode::kAccessExclusive
    );

    auto retried = backend->finish_command(CommandOutcome::kFailed);
    ASSERT_TRUE(retried);
    EXPECT_EQ(retried->disposition, TransactionDisposition::kAbort);
    EXPECT_EQ(backend->state(), TransactionBlockState::kIdle);
    EXPECT_EQ(backend->xid(), base::kInvalidXid);
    EXPECT_TRUE((*manager_result)->commit_log().is_aborted(xid));
    EXPECT_FALSE((*manager_result)->lock_manager().held_table_mode(xid, base::RelationId{12}));
    ASSERT_TRUE(backend->close());
}

TEST(TransactionBackendTest, FailedExplicitRollbackRetainsBlockForRetry) {
    auto completion_journal = std::make_unique<test::RecordingCompletionJournal>();
    auto* journal_view = completion_journal.get();
    auto manager_result =
        TransactionManager::with_completion_journal(std::move(completion_journal));
    ASSERT_TRUE(manager_result);
    auto backend = create_backend(**manager_result);
    auto started = backend->begin_explicit();
    ASSERT_TRUE(started);
    const auto xid = started->xid;

    journal_view->fail_next_append(CompletionOutcome::kAbort);
    auto failed = backend->rollback_explicit();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(backend->state(), TransactionBlockState::kInTransaction);
    EXPECT_EQ(backend->scope(), TransactionScope::kExplicit);
    EXPECT_EQ(backend->xid(), xid);
    EXPECT_TRUE((*manager_result)->commit_log().is_in_progress(xid));

    ASSERT_TRUE(backend->rollback_explicit());
    EXPECT_EQ(backend->state(), TransactionBlockState::kIdle);
    EXPECT_EQ(backend->xid(), base::kInvalidXid);
    EXPECT_TRUE((*manager_result)->commit_log().is_aborted(xid));
    ASSERT_TRUE(backend->close());
}

TEST(TransactionBackendTest, FailedCommitAbortRetainsOriginalDiagnosticAndRollbackEntry) {
    auto completion_journal = std::make_unique<test::RecordingCompletionJournal>();
    auto* journal_view = completion_journal.get();
    auto manager_result =
        TransactionManager::with_completion_journal(std::move(completion_journal));
    ASSERT_TRUE(manager_result);
    auto backend = create_backend(**manager_result);
    auto started = backend->begin_explicit();
    ASSERT_TRUE(started);
    const auto xid = started->xid;

    journal_view->fail_next_flush();
    journal_view->fail_next_append(CompletionOutcome::kAbort);
    auto failed = backend->commit_explicit();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_NE(
        failed.error().to_string().find("injected completion journal flush failure"),
        std::string::npos
    );
    EXPECT_EQ(backend->state(), TransactionBlockState::kFailedTransaction);
    EXPECT_EQ(backend->scope(), TransactionScope::kExplicit);
    EXPECT_FALSE(backend->command_active());
    EXPECT_EQ(backend->xid(), xid);
    EXPECT_TRUE((*manager_result)->commit_log().is_in_progress(xid));

    ASSERT_TRUE(backend->rollback_explicit());
    EXPECT_EQ(backend->state(), TransactionBlockState::kIdle);
    EXPECT_EQ(backend->xid(), base::kInvalidXid);
    EXPECT_TRUE((*manager_result)->commit_log().is_aborted(xid));
    ASSERT_TRUE(backend->close());
}

TEST(TransactionBackendTest, SerializablePrecommitFailureRollsBackAllSharedState) {
    TransactionManager manager;
    auto backend = create_backend(manager);
    ASSERT_TRUE(backend->set_isolation(IsolationLevel::kSerializable));
    auto started = backend->begin_explicit();
    ASSERT_TRUE(started);
    ASSERT_TRUE(started->snapshot);
    const auto xid = started->xid;
    EXPECT_EQ(manager.ssi_manager().transaction_count(), 1U);
    ASSERT_TRUE(manager.lock_manager()
                    .acquire_table(xid, base::RelationId{14}, TableLockMode::kRowExclusive));
    ASSERT_TRUE(manager.ssi_manager().mark_doomed(xid));

    auto commit = backend->commit_explicit();
    ASSERT_FALSE(commit);
    EXPECT_EQ(commit.error().code(), base::ErrorCode::kSerializationFailure);
    EXPECT_TRUE(commit.error().is_retryable());
    EXPECT_TRUE(manager.commit_log().is_aborted(xid));
    EXPECT_FALSE(backend->proc_slot()->has_active_transaction());
    EXPECT_EQ(manager.ssi_manager().transaction_count(), 0U);
    EXPECT_EQ(manager.lock_manager().stats(), LockManagerStats{});
    ASSERT_TRUE(backend->close());
}

TEST(TransactionBackendTest, SuccessfulSerializableReadOnlyCommitFinalizesAndPrunesSsi) {
    TransactionManager manager;
    auto backend =
        create_backend(manager, TransactionBackendOptions{.track_persistent_writes = true});
    ASSERT_TRUE(backend->set_isolation(IsolationLevel::kSerializable));
    auto started = backend->begin_explicit();
    ASSERT_TRUE(started);
    ASSERT_TRUE(started->snapshot);
    const auto xid = started->xid;
    auto command = backend->start_command();
    ASSERT_TRUE(command);
    EXPECT_EQ(command->transition.snapshot_action, SnapshotAction::kReuseTransaction);
    ASSERT_TRUE(backend->finish_command(CommandOutcome::kSucceeded));
    ASSERT_TRUE(backend->commit_explicit());
    EXPECT_TRUE(manager.commit_log().is_committed(xid));
    EXPECT_EQ(manager.ssi_manager().transaction_count(), 0U);
    ASSERT_TRUE(backend->close());
}

TEST(TransactionBackendTest, IsolationChangeRegistersSsiBeforeFirstCommandSnapshot) {
    TransactionManager manager;
    auto backend = create_backend(manager);
    auto started = backend->begin_explicit();
    ASSERT_TRUE(started);
    EXPECT_FALSE(started->snapshot);
    ASSERT_TRUE(backend->set_isolation(IsolationLevel::kSerializable));
    auto command = backend->start_command();
    ASSERT_TRUE(command);
    EXPECT_EQ(command->transition.snapshot_action, SnapshotAction::kCaptureTransaction);
    EXPECT_NE(manager.ssi_manager().snapshot_sequence(command->xid), 0U);
    ASSERT_TRUE(backend->finish_command(CommandOutcome::kSucceeded));
    ASSERT_TRUE(backend->rollback_explicit());
    ASSERT_TRUE(backend->close());
}

TEST(TransactionBackendTest, NonSerializableIsolationLevelsBypassSsi) {
    TransactionManager manager;
    auto read_committed = create_backend(manager);
    ASSERT_TRUE(read_committed->start_command());
    EXPECT_EQ(manager.ssi_manager().transaction_count(), 0U);
    ASSERT_TRUE(read_committed->finish_command(CommandOutcome::kSucceeded));
    ASSERT_TRUE(read_committed->close());

    auto repeatable_read = create_backend(manager);
    ASSERT_TRUE(repeatable_read->set_isolation(IsolationLevel::kRepeatableRead));
    auto started = repeatable_read->begin_explicit();
    ASSERT_TRUE(started);
    EXPECT_TRUE(started->snapshot);
    EXPECT_EQ(manager.ssi_manager().transaction_count(), 0U);
    ASSERT_TRUE(repeatable_read->rollback_explicit());
    ASSERT_TRUE(repeatable_read->close());
}

TEST(TransactionBackendTest, DestroyedLiveBackendRemainsVisibleUntilManagerRollback) {
    TransactionManager manager;
    base::Xid abandoned_xid{base::kInvalidXid};
    {
        auto backend = create_backend(manager);
        auto started = backend->begin_explicit();
        ASSERT_TRUE(started);
        abandoned_xid = started->xid;
        ASSERT_TRUE(manager.lock_manager().acquire_table(
            abandoned_xid,
            base::RelationId{17},
            TableLockMode::kAccessExclusive
        ));
        auto premature_close = backend->close();
        ASSERT_FALSE(premature_close);
        EXPECT_EQ(premature_close.error().code(), base::ErrorCode::kTransactionState);
    }

    EXPECT_EQ(manager.proc_array().registered_session_count(), 1U);
    EXPECT_EQ(
        manager.proc_array().abandoned_transactions(),
        std::vector<base::Xid>({abandoned_xid})
    );
    EXPECT_TRUE(manager.commit_log().is_in_progress(abandoned_xid));
    auto recovered = manager.rollback_abandoned_transactions();
    ASSERT_TRUE(recovered);
    EXPECT_EQ(*recovered, 1U);
    EXPECT_TRUE(manager.commit_log().is_aborted(abandoned_xid));
    EXPECT_EQ(manager.proc_array().registered_session_count(), 0U);
    EXPECT_EQ(manager.lock_manager().stats(), LockManagerStats{});
    auto repeated = manager.rollback_abandoned_transactions();
    ASSERT_TRUE(repeated);
    EXPECT_EQ(*repeated, 0U);
}

TEST(TransactionBackendTest, ActiveSiblingThresholdSelectsBoundedGroupDelay) {
    constexpr std::size_t k_backends = kTransactionGroupCommitSiblings + 1U;
    EXPECT_EQ(kTransactionGroupCommitDelay, std::chrono::microseconds{250});
    EXPECT_EQ(kTransactionGroupCommitSiblings, 5U);

    auto completion_journal = std::make_unique<test::RecordingCompletionJournal>();
    auto* journal_view = completion_journal.get();
    auto manager_result =
        TransactionManager::with_completion_journal(std::move(completion_journal));
    ASSERT_TRUE(manager_result);
    std::vector<std::unique_ptr<TransactionBackend>> backends;
    backends.reserve(k_backends);
    for (std::size_t index = 0; index < k_backends; ++index) {
        auto backend = create_backend(**manager_result);
        ASSERT_TRUE(backend->begin_explicit());
        backends.push_back(std::move(backend));
    }

    ASSERT_TRUE(backends[0]->commit_explicit());
    ASSERT_TRUE(backends[1]->commit_explicit());
    const auto flushes = journal_view->flushes();
    ASSERT_EQ(flushes.size(), 2U);
    EXPECT_EQ(flushes[0].group_delay, kTransactionGroupCommitDelay);
    EXPECT_EQ(flushes[1].group_delay, std::chrono::microseconds::zero());

    ASSERT_TRUE(backends[0]->close());
    ASSERT_TRUE(backends[1]->close());
    for (std::size_t index = 2U; index < backends.size(); ++index) {
        ASSERT_TRUE(backends[index]->rollback_explicit());
        ASSERT_TRUE(backends[index]->close());
    }
}

}  // namespace
}  // namespace db::transaction
