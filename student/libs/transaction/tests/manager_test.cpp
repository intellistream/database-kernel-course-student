#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/manager.hpp"
#include "db/transaction/proc_array.hpp"
#include "db/transaction/visibility.hpp"

#include "completion_journal_test_support.hpp"

namespace db::transaction {
namespace {

using namespace std::chrono_literals;

inline constexpr std::uint64_t kSystemIdentifier = 0x8877'6655'4433'2211ULL;

enum class JournalThrowPoint : std::uint8_t {
    kAppend,
    kFlush,
};

enum class JournalException : std::uint8_t {
    kAllocation,
    kLength,
    kStandard,
    kUnknown,
};

class OneShotThrowingCompletionJournal final : public CompletionJournalPort {
public:
    OneShotThrowingCompletionJournal(
        const JournalThrowPoint point,
        const JournalException exception
    ) noexcept
        : point_{point},
          exception_{exception} {}

    auto append_completion(const base::Xid xid, const CompletionOutcome outcome)
        -> base::Result<base::Lsn> override {
        static_cast<void>(xid);
        static_cast<void>(outcome);
        if (auto exception = take_exception(JournalThrowPoint::kAppend); exception.has_value()) {
            throw_exception(*exception);
        }
        return base::Lsn{8U};
    }

    auto flush_through(const base::Lsn end_lsn, const std::chrono::microseconds group_delay)
        -> base::Status override {
        static_cast<void>(end_lsn);
        static_cast<void>(group_delay);
        if (auto exception = take_exception(JournalThrowPoint::kFlush); exception.has_value()) {
            throw_exception(*exception);
        }
        return {};
    }

private:
    auto take_exception(const JournalThrowPoint point) -> std::optional<JournalException> {
        const std::lock_guard lock{mutex_};
        if (!armed_ || point_ != point) {
            return std::nullopt;
        }
        armed_ = false;
        return exception_;
    }

    [[noreturn]] static void throw_exception(const JournalException exception) {
        switch (exception) {
            case JournalException::kAllocation:
                throw std::bad_alloc{};
            case JournalException::kLength:
                throw std::length_error{"injected completion journal length failure"};
            case JournalException::kStandard:
                throw std::runtime_error{"injected completion journal exception"};
            case JournalException::kUnknown:
                throw 7;
        }
        std::terminate();
    }

    std::mutex mutex_;
    JournalThrowPoint point_;
    JournalException exception_;
    bool armed_{true};
};

auto register_session(TransactionManager& manager) -> ProcSlotLease {
    auto registered = manager.register_session();
    EXPECT_TRUE(registered);
    return std::move(*registered);
}

auto assign_xid(TransactionManager& manager, ProcSlotLease& lease) -> base::Xid {
    auto xid = manager.assign_xid(lease);
    EXPECT_TRUE(xid);
    return *xid;
}

auto clean_startup_state(
    const std::shared_ptr<CommitLog>& commit_log,
    const XidHorizon horizon = {}
) -> TransactionStartupState {
    auto startup = TransactionStartupState::from_clean_state(commit_log, horizon);
    EXPECT_TRUE(startup);
    return std::move(*startup);
}

using StressResult = std::pair<std::uint32_t, TransactionStatus>;

auto run_mixed_transaction_session(
    TransactionManager& manager,
    std::size_t session,
    std::size_t transaction_count,
    std::barrier<>& ready,
    std::mutex& results_mutex,
    std::vector<StressResult>& results
) -> bool {
    auto registered = manager.register_session();
    if (!registered) {
        ready.arrive_and_wait();
        return false;
    }
    auto lease = std::move(*registered);
    auto snapshots = manager.snapshot_coordinator(lease);
    ready.arrive_and_wait();
    for (std::size_t transaction = 0; transaction < transaction_count; ++transaction) {
        auto assigned = manager.assign_xid(lease);
        if (!assigned) {
            return false;
        }
        const auto xid = *assigned;
        auto captured = snapshots.acquire(
            CommandStart{
                .transaction_epoch = transaction + 1U,
                .command_id = 1U,
                .isolation = IsolationLevel::kReadCommitted,
                .scope = TransactionScope::kImplicit,
                .snapshot_action = SnapshotAction::kCaptureCommand,
                .starts_transaction = true,
            }
        );
        if (!captured) {
            return false;
        }
        if (!manager.lock_manager()
                 .acquire_table(xid, base::RelationId{77}, TableLockMode::kRowExclusive)) {
            return false;
        }
        const auto rid = base::Rid{
            base::PageId{static_cast<std::uint32_t>(session)},
            base::SlotId{static_cast<std::uint16_t>(transaction)}
        };
        if (!manager.lock_manager()
                 .acquire_row(xid, base::RelationId{77}, rid, RowLockMode::kExclusive)) {
            return false;
        }
        const auto final_status = (session + transaction) % 3U == 0 ? TransactionStatus::kAborted
                                                                    : TransactionStatus::kCommitted;
        if (!manager.complete(lease, xid, final_status)) {
            return false;
        }
        const std::lock_guard result_lock{results_mutex};
        results.emplace_back(xid.value(), final_status);
    }
    return static_cast<bool>(lease.close());
}

TEST(TransactionManagerTest, PublishesSharedCommitAndAbortStateWithoutWal) {
    TransactionManager manager;
    auto first = register_session(manager);
    auto second = register_session(manager);
    const auto first_xid = assign_xid(manager, first);
    const auto second_xid = assign_xid(manager, second);
    EXPECT_EQ(first_xid, base::Xid{1});
    EXPECT_EQ(second_xid, base::Xid{2});
    EXPECT_EQ(manager.lock_manager().stats().transaction_targets, 2U);
    EXPECT_EQ(manager.commit_log().segment_count(), 1U);
    ASSERT_TRUE(manager.lock_manager().acquire_table(
        first_xid,
        base::RelationId{7},
        TableLockMode::kAccessExclusive
    ));
    ASSERT_TRUE(manager.lock_manager()
                    .acquire_table(second_xid, base::RelationId{8}, TableLockMode::kRowExclusive));
    EXPECT_EQ(manager.lock_manager().stats().granted_locks, 2U);

    auto in_progress = manager.complete(first, first_xid, TransactionStatus::kInProgress);
    ASSERT_FALSE(in_progress);
    EXPECT_EQ(in_progress.error().code(), base::ErrorCode::kInvalidArgument);
    auto wrong_xid = manager.complete(first, second_xid, TransactionStatus::kCommitted);
    ASSERT_FALSE(wrong_xid);
    EXPECT_EQ(wrong_xid.error().code(), base::ErrorCode::kTransactionState);
    EXPECT_TRUE(first.slot()->has_active_transaction());

    ASSERT_TRUE(manager.complete(first, first_xid, TransactionStatus::kCommitted));
    EXPECT_TRUE(manager.commit_log().is_committed(first_xid));
    EXPECT_FALSE(first.slot()->has_active_transaction());
    EXPECT_FALSE(manager.lock_manager().held_table_mode(first_xid, base::RelationId{7}));
    ASSERT_TRUE(manager.complete(second, second_xid, TransactionStatus::kAborted));
    EXPECT_TRUE(manager.commit_log().is_aborted(second_xid));
    EXPECT_EQ(manager.lock_manager().stats(), LockManagerStats{});
    EXPECT_EQ(
        manager.xid_horizon(),
        (XidHorizon{.next_xid = base::Xid{3}, .latest_completed_xid = base::Xid{2}})
    );
    ASSERT_TRUE(manager.flush_commit_log());
    ASSERT_TRUE(first.close());
    ASSERT_TRUE(second.close());
}

TEST(TransactionManagerTest, CompletionJournalFlushesBeforeCommitPublication) {
    auto commit_log = std::make_shared<CommitLog>();
    auto completion_journal = std::make_unique<test::RecordingCompletionJournal>();
    auto* journal_view = completion_journal.get();
    auto manager_result = TransactionManager::from_startup_state(
        clean_startup_state(commit_log),
        std::move(completion_journal)
    );
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;

    auto committed = register_session(manager);
    const auto committed_xid = assign_xid(manager, committed);
    ASSERT_TRUE(manager.complete(committed, committed_xid, TransactionStatus::kCommitted));
    EXPECT_TRUE(commit_log->is_committed(committed_xid));
    EXPECT_FALSE(committed.slot()->has_active_transaction());

    auto aborted = register_session(manager);
    const auto aborted_xid = assign_xid(manager, aborted);
    ASSERT_TRUE(manager.complete(aborted, aborted_xid, TransactionStatus::kAborted));
    EXPECT_TRUE(commit_log->is_aborted(aborted_xid));

    const auto appends = journal_view->appends();
    ASSERT_EQ(appends.size(), 2U);
    EXPECT_EQ(appends[0].xid, committed_xid);
    EXPECT_EQ(appends[0].outcome, CompletionOutcome::kCommit);
    EXPECT_EQ(appends[1].xid, aborted_xid);
    EXPECT_EQ(appends[1].outcome, CompletionOutcome::kAbort);
    const auto flushes = journal_view->flushes();
    ASSERT_EQ(flushes.size(), 1U);
    EXPECT_EQ(flushes[0].end_lsn, appends[0].end_lsn);
    EXPECT_EQ(flushes[0].group_delay, std::chrono::microseconds::zero());
    ASSERT_TRUE(committed.close());
    ASSERT_TRUE(aborted.close());
}

TEST(TransactionManagerTest, UnloggedCommitFlushesClogWithoutCompletionJournal) {
    auto root = test_support::TestDirectory::create("transaction-completion-unlogged");
    ASSERT_TRUE(root);
    auto commit_log_result = CommitLog::open(
        root->join("xact"),
        CommitLogOpenOptions{.system_identifier = kSystemIdentifier, .create = true}
    );
    ASSERT_TRUE(commit_log_result);
    auto commit_log = std::make_shared<CommitLog>(std::move(*commit_log_result));
    auto completion_journal = std::make_unique<test::RecordingCompletionJournal>();
    auto* journal_view = completion_journal.get();
    auto manager_result = TransactionManager::from_startup_state(
        clean_startup_state(commit_log),
        std::move(completion_journal)
    );
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;

    auto lease = register_session(manager);
    const auto xid = assign_xid(manager, lease);
    ASSERT_TRUE(manager.complete_unlogged(lease, xid, TransactionStatus::kCommitted));
    EXPECT_EQ(journal_view->append_count(), 0U);
    EXPECT_EQ(journal_view->flush_count(), 0U);
    EXPECT_TRUE(commit_log->is_committed(xid));

    auto reopened = CommitLog::open(
        root->join("xact"),
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
        }
    );
    ASSERT_TRUE(reopened);
    EXPECT_TRUE(reopened->is_committed(xid));
    ASSERT_TRUE(lease.close());
}

TEST(TransactionManagerTest, FailedJournalFlushLeavesStatusAndSlotInProgress) {
    auto completion_journal = std::make_unique<test::RecordingCompletionJournal>();
    auto* journal_view = completion_journal.get();
    auto manager_result =
        TransactionManager::with_completion_journal(std::move(completion_journal));
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;
    auto lease = register_session(manager);
    const auto xid = assign_xid(manager, lease);
    ASSERT_TRUE(manager.lock_manager()
                    .acquire_table(xid, base::RelationId{12}, TableLockMode::kAccessExclusive));

    journal_view->fail_next_flush();
    auto failed = manager.complete(lease, xid, TransactionStatus::kCommitted);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_TRUE(manager.commit_log().is_in_progress(xid));
    EXPECT_EQ(lease.slot()->xid(), xid);
    EXPECT_EQ(manager.xid_horizon().latest_completed_xid, base::kInvalidXid);
    EXPECT_EQ(
        manager.lock_manager().held_table_mode(xid, base::RelationId{12}),
        TableLockMode::kAccessExclusive
    );
    EXPECT_EQ(manager.lock_manager().stats().transaction_targets, 1U);
    EXPECT_EQ(journal_view->append_count(), 1U);
    EXPECT_EQ(journal_view->flush_count(), 1U);

    ASSERT_TRUE(manager.complete(lease, xid, TransactionStatus::kCommitted));
    EXPECT_TRUE(manager.commit_log().is_committed(xid));
    EXPECT_FALSE(lease.slot()->has_active_transaction());
    EXPECT_EQ(manager.xid_horizon().latest_completed_xid, xid);
    EXPECT_EQ(manager.lock_manager().stats(), LockManagerStats{});
    EXPECT_EQ(journal_view->append_count(), 2U);
    EXPECT_EQ(journal_view->flush_count(), 2U);
    ASSERT_TRUE(lease.close());
}

TEST(TransactionManagerTest, CompletionJournalAppendExceptionsAreStructuredAndRetryable) {
    constexpr std::array cases{
        std::pair{JournalException::kAllocation, base::ErrorCode::kResourceExhausted},
        std::pair{JournalException::kLength, base::ErrorCode::kResourceExhausted},
        std::pair{JournalException::kStandard, base::ErrorCode::kInternal},
        std::pair{JournalException::kUnknown, base::ErrorCode::kInternal},
    };

    for (const auto& [exception, expected_code] : cases) {
        auto manager_result = TransactionManager::with_completion_journal(
            std::make_unique<OneShotThrowingCompletionJournal>(
                JournalThrowPoint::kAppend,
                exception
            )
        );
        ASSERT_TRUE(manager_result);
        auto& manager = **manager_result;
        auto lease = register_session(manager);
        const auto xid = assign_xid(manager, lease);

        auto failed = manager.complete(lease, xid, TransactionStatus::kAborted);
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error().code(), expected_code);
        EXPECT_TRUE(manager.commit_log().is_in_progress(xid));
        ASSERT_NE(lease.slot(), nullptr);
        EXPECT_EQ(lease.slot()->xid(), xid);

        ASSERT_TRUE(manager.complete(lease, xid, TransactionStatus::kAborted));
        EXPECT_TRUE(manager.commit_log().is_aborted(xid));
        EXPECT_FALSE(lease.slot()->has_active_transaction());
        ASSERT_TRUE(lease.close());
    }
}

TEST(TransactionManagerTest, CompletionJournalFlushExceptionIsStructuredAndRetryable) {
    auto manager_result = TransactionManager::with_completion_journal(
        std::make_unique<OneShotThrowingCompletionJournal>(
            JournalThrowPoint::kFlush,
            JournalException::kStandard
        )
    );
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;
    auto lease = register_session(manager);
    const auto xid = assign_xid(manager, lease);

    auto failed = manager.complete(lease, xid, TransactionStatus::kCommitted);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(
        failed.error().to_string().find("injected completion journal exception"),
        std::string::npos
    );
    EXPECT_TRUE(manager.commit_log().is_in_progress(xid));
    ASSERT_NE(lease.slot(), nullptr);
    EXPECT_EQ(lease.slot()->xid(), xid);

    ASSERT_TRUE(manager.complete(lease, xid, TransactionStatus::kCommitted));
    EXPECT_TRUE(manager.commit_log().is_committed(xid));
    EXPECT_FALSE(lease.slot()->has_active_transaction());
    ASSERT_TRUE(lease.close());
}

TEST(TransactionManagerTest, AbandonedJournalExceptionLeavesRecoveryRetryable) {
    auto manager_result = TransactionManager::with_completion_journal(
        std::make_unique<OneShotThrowingCompletionJournal>(
            JournalThrowPoint::kAppend,
            JournalException::kUnknown
        )
    );
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;
    base::Xid xid{base::kInvalidXid};
    {
        auto lease = register_session(manager);
        xid = assign_xid(manager, lease);
    }

    auto failed = manager.rollback_abandoned_transactions();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kInternal);
    EXPECT_TRUE(manager.commit_log().is_in_progress(xid));
    EXPECT_EQ(manager.proc_array().abandoned_transactions(), std::vector<base::Xid>{xid});

    auto retried = manager.rollback_abandoned_transactions();
    ASSERT_TRUE(retried);
    EXPECT_EQ(*retried, 1U);
    EXPECT_TRUE(manager.commit_log().is_aborted(xid));
    EXPECT_TRUE(manager.proc_array().abandoned_transactions().empty());
}

TEST(TransactionManagerTest, FailedClogPreparationPublishesNeitherXidNorTag) {
    auto commit_log = std::make_shared<CommitLog>();
    auto startup = TransactionStartupState::from_clean_state(commit_log, XidHorizon{});
    ASSERT_TRUE(startup);
    auto manager_result = TransactionManager::from_startup_state(std::move(*startup));
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;
    auto lease = register_session(manager);

    CommitLog moved_commit_log{std::move(*commit_log)};
    auto failed = manager.assign_xid(lease);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_FALSE(lease.slot()->has_active_transaction());
    EXPECT_EQ(manager.xid_horizon(), XidHorizon{});
    EXPECT_EQ(manager.lock_manager().stats(), LockManagerStats{});
    ASSERT_TRUE(lease.close());
    EXPECT_TRUE(moved_commit_log.valid());
}

TEST(TransactionManagerTest, RecoveryStateValidatesClogAndControlsNextAssignment) {
    auto commit_log = std::make_shared<CommitLog>();
    ASSERT_TRUE(commit_log->set_status(base::Xid{40}, TransactionStatus::kCommitted));
    auto unfinished_gap = TransactionStartupState::from_recovered_state(
        commit_log,
        XidHorizon{.next_xid = base::Xid{41}, .latest_completed_xid = base::Xid{40}}
    );
    ASSERT_FALSE(unfinished_gap);
    EXPECT_EQ(unfinished_gap.error().code(), base::ErrorCode::kTransactionState);
    auto different_log = std::make_shared<CommitLog>();
    ASSERT_TRUE(different_log->ensure_xid_allocated(base::Xid{40}));
    auto foreign_frontier = TransactionStartupState::from_recovered_state(
        different_log,
        XidHorizon{.next_xid = base::Xid{41}, .latest_completed_xid = base::Xid{40}}
    );
    ASSERT_FALSE(foreign_frontier);
    EXPECT_EQ(foreign_frontier.error().code(), base::ErrorCode::kTransactionState);
    ASSERT_TRUE(commit_log->abort_in_progress_before(base::Xid{41}));
    auto startup = TransactionStartupState::from_recovered_state(
        commit_log,
        XidHorizon{.next_xid = base::Xid{41}, .latest_completed_xid = base::Xid{40}}
    );
    ASSERT_TRUE(startup);
    auto manager_result = TransactionManager::from_startup_state(std::move(*startup));
    ASSERT_TRUE(manager_result);
    auto lease = register_session(**manager_result);
    EXPECT_EQ(assign_xid(**manager_result, lease), base::Xid{41});
    ASSERT_TRUE((*manager_result)->complete(lease, base::Xid{41}, TransactionStatus::kAborted));
    ASSERT_TRUE(lease.close());

    auto inconsistent_log = std::make_shared<CommitLog>();
    auto inconsistent = TransactionStartupState::from_clean_state(
        inconsistent_log,
        XidHorizon{.next_xid = base::Xid{11}, .latest_completed_xid = base::Xid{10}}
    );
    ASSERT_FALSE(inconsistent);
    EXPECT_EQ(inconsistent.error().code(), base::ErrorCode::kCorruption);
    auto mismatched_completion_log = std::make_shared<CommitLog>();
    ASSERT_TRUE(mismatched_completion_log->ensure_xid_allocated(base::Xid{10}));
    ASSERT_TRUE(mismatched_completion_log->abort_in_progress_before(base::Xid{11}));
    auto mismatched_completion = TransactionStartupState::from_clean_state(
        mismatched_completion_log,
        XidHorizon{.next_xid = base::Xid{11}, .latest_completed_xid = base::Xid{9}}
    );
    ASSERT_FALSE(mismatched_completion);
    EXPECT_EQ(mismatched_completion.error().code(), base::ErrorCode::kCorruption);
    auto ahead_log = std::make_shared<CommitLog>();
    ASSERT_TRUE(ahead_log->ensure_xid_allocated(base::Xid{11}));
    auto ahead = TransactionStartupState::from_clean_state(
        ahead_log,
        XidHorizon{.next_xid = base::Xid{11}, .latest_completed_xid = base::Xid{10}}
    );
    ASSERT_FALSE(ahead);
    EXPECT_EQ(ahead.error().code(), base::ErrorCode::kCorruption);
    auto null_log = TransactionStartupState::from_clean_state(nullptr, XidHorizon{});
    ASSERT_FALSE(null_log);
    EXPECT_EQ(null_log.error().code(), base::ErrorCode::kInvalidArgument);
    auto null_journal = TransactionManager::with_completion_journal(nullptr);
    ASSERT_FALSE(null_journal);
    EXPECT_EQ(null_journal.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(TransactionManagerTest, CheckpointBarrierExcludesCompletionPublicationInterval) {
    TransactionManager manager;
    auto lease = register_session(manager);
    const auto xid = assign_xid(manager, lease);
    std::optional<CheckpointCompletionBarrier> barrier{manager.checkpoint_completion_barrier()};
    std::atomic<bool> started{false};
    std::atomic<bool> completed{false};
    std::thread worker{[&] {
        started.store(true, std::memory_order_release);
        if (manager.complete(lease, xid, TransactionStatus::kCommitted)) {
            completed.store(true, std::memory_order_release);
        }
    }};
    while (!started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(20ms);
    EXPECT_FALSE(completed.load(std::memory_order_acquire));
    EXPECT_TRUE(manager.commit_log().is_in_progress(xid));
    EXPECT_EQ(lease.slot()->xid(), xid);

    barrier.reset();
    worker.join();
    EXPECT_TRUE(completed.load(std::memory_order_acquire));
    EXPECT_TRUE(manager.commit_log().is_committed(xid));
    EXPECT_FALSE(lease.slot()->has_active_transaction());
    ASSERT_TRUE(lease.close());

    auto short_lived_manager = std::make_unique<TransactionManager>();
    std::optional<CheckpointCompletionBarrier> late_barrier{
        short_lived_manager->checkpoint_completion_barrier()
    };
    short_lived_manager.reset();
    late_barrier.reset();
}

TEST(TransactionManagerTest, ConcurrentMixedCommitAbortSnapshotAndLockStress) {
    constexpr std::size_t k_sessions = 12;
    constexpr std::size_t k_transactions_per_session = 100;
    TransactionManager manager;
    std::barrier ready{static_cast<std::ptrdiff_t>(k_sessions)};
    std::atomic<bool> succeeded{true};
    std::mutex results_mutex;
    std::vector<StressResult> results;
    results.reserve(k_sessions * k_transactions_per_session);
    std::vector<std::thread> workers;
    workers.reserve(k_sessions);

    for (std::size_t session = 0; session < k_sessions; ++session) {
        workers.emplace_back([&, session] {
            if (!run_mixed_transaction_session(
                    manager,
                    session,
                    k_transactions_per_session,
                    ready,
                    results_mutex,
                    results
                )) {
                succeeded.store(false, std::memory_order_release);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    ASSERT_TRUE(succeeded.load(std::memory_order_acquire));
    ASSERT_EQ(results.size(), k_sessions * k_transactions_per_session);
    std::ranges::sort(results, {}, &StressResult::first);
    for (std::size_t index = 0; index < results.size(); ++index) {
        EXPECT_EQ(results[index].first, index + 1U);
        EXPECT_EQ(
            manager.commit_log().status(base::Xid{results[index].first}),
            results[index].second
        );
    }
    EXPECT_EQ(manager.proc_array().registered_session_count(), 0U);
    EXPECT_EQ(manager.proc_array().oldest_active_xmin(), kXidWrapBoundary);
    EXPECT_EQ(manager.lock_manager().stats(), LockManagerStats{});
    EXPECT_EQ(
        manager.xid_horizon(),
        (XidHorizon{
            .next_xid = base::Xid{static_cast<std::uint32_t>(results.size() + 1U)},
            .latest_completed_xid = base::Xid{static_cast<std::uint32_t>(results.size())}
        })
    );
}

}  // namespace
}  // namespace db::transaction
