#include "db/transaction/manager.hpp"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/visibility.hpp"

#include "transaction_completion.hpp"
#include "xid_assignment.hpp"

namespace db::transaction {
namespace {

[[maybe_unused]] auto
manager_error(base::ErrorCode code, std::string message, std::string operation) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "transaction/manager",
        std::move(operation)
    );
}

[[maybe_unused]] auto is_final(TransactionStatus status) noexcept -> bool {
    return status == TransactionStatus::kCommitted || status == TransactionStatus::kAborted;
}

[[maybe_unused]] auto
validate_startup_pair(const std::shared_ptr<CommitLog>& commit_log, const XidHorizon horizon)
    -> base::Status {
    if (commit_log == nullptr || !commit_log->valid()) {
        return base::fail(manager_error(
            base::ErrorCode::kInvalidArgument,
            "startup state requires an active CLOG",
            "prepare startup state"
        ));
    }
    if (horizon.next_xid == base::kInvalidXid
        || (!is_normal_xid(horizon.next_xid) && horizon.next_xid != kXidWrapBoundary)) {
        return base::fail(manager_error(
            base::ErrorCode::kInvalidArgument,
            "startup next XID must be normal or the exhausted boundary",
            "prepare startup state"
        ));
    }
    const auto expected_latest = horizon.next_xid == kFirstNormalXid
                                     ? base::kInvalidXid
                                     : base::Xid{horizon.next_xid.value() - 1U};
    if (horizon.latest_completed_xid != expected_latest) {
        return base::fail(manager_error(
            base::ErrorCode::kCorruption,
            "startup completion horizon does not end immediately before its next XID",
            "prepare startup state"
        ));
    }
    if (commit_log->next_xid() != horizon.next_xid) {
        return base::fail(manager_error(
            base::ErrorCode::kCorruption,
            "startup CLOG allocation horizon does not match its next XID",
            "prepare startup state"
        ));
    }
    return {};
}

inline constexpr base::ExceptionBoundaryPolicy kTransactionTagExceptionPolicy{
    .component = "transaction/manager",
    .allocation_message = "transaction tag allocation failed",
    .exception_prefix = "transaction tag publication threw: ",
    .unknown_exception_message = "transaction tag publication threw an unknown exception",
};

inline constexpr base::ExceptionBoundaryPolicy kCommitLogAllocationExceptionPolicy{
    .component = "transaction/manager",
    .allocation_message = "CLOG allocation preparation failed",
    .exception_prefix = "CLOG allocation preparation threw: ",
    .unknown_exception_message = "CLOG allocation preparation threw an unknown exception",
};

class ManagerXidAssignmentPreparation final : public detail::XidAssignmentPreparation {
public:
    explicit ManagerXidAssignmentPreparation(CommitLog& commit_log) noexcept
        : commit_log_{commit_log} {}

    [[maybe_unused]] auto prepare(const base::Xid candidate) -> base::Status override {
        return base::invoke_exception_boundary(
            kCommitLogAllocationExceptionPolicy,
            "prepare CLOG allocation",
            [&] {
                return commit_log_.ensure_xid_allocated(candidate);
            }
        );
    }

private:
    CommitLog& commit_log_;
};

}  // namespace

TransactionStartupState::TransactionStartupState(
    std::shared_ptr<CommitLog> commit_log,
    const XidHorizon horizon
) noexcept
    : commit_log_{std::move(commit_log)},
      horizon_{horizon} {}

auto TransactionStartupState::from_clean_state(
    std::shared_ptr<CommitLog> commit_log,
    const XidHorizon horizon
) -> base::Result<TransactionStartupState> {
    if (auto status = validate_startup_pair(commit_log, horizon); !status) {
        return base::fail(std::move(status).error());
    }
    return TransactionStartupState{std::move(commit_log), horizon};
}

auto TransactionStartupState::from_recovered_state(
    std::shared_ptr<CommitLog> commit_log,
    const XidHorizon horizon
) -> base::Result<TransactionStartupState> {
    if (auto status = validate_startup_pair(commit_log, horizon); !status) {
        return base::fail(std::move(status).error());
    }
    if (auto status = commit_log->require_recovery_resolved_before(horizon.next_xid); !status) {
        return base::fail(
            std::move(status).error().with_context(
                "transaction/manager",
                "prepare recovered startup state"
            )
        );
    }
    return TransactionStartupState{std::move(commit_log), horizon};
}

struct CheckpointCompletionBarrier::Inner final {
    explicit Inner(std::shared_ptr<std::shared_mutex> gate)
        : gate_owner{std::move(gate)},
          lock{*gate_owner} {}

    std::shared_ptr<std::shared_mutex> gate_owner;
    std::unique_lock<std::shared_mutex> lock;
};

struct TransactionManager::Inner final {
    Inner(
        std::shared_ptr<CommitLog> stored_commit_log,
        std::unique_ptr<ProcArray> stored_proc_array,
        const LockManagerOptions& lock_options,
        std::unique_ptr<CompletionJournalPort> stored_completion_journal
    ) noexcept
        : commit_log{std::move(stored_commit_log)},
          proc_array{std::move(stored_proc_array)},
          lock_manager{lock_options},
          ssi_manager{},
          xid_waiter{TransactionManager::make_xid_waiter(lock_manager)},
          completion_journal{std::move(stored_completion_journal)} {}

    auto completion_services() noexcept -> detail::TransactionCompletionServices {
        return detail::TransactionCompletionServices{
            .commit_log = *commit_log,
            .proc_array = *proc_array,
            .lock_manager = lock_manager,
            .completion_journal = completion_journal.get(),
            .completion_gate = *completion_gate,
        };
    }

    std::shared_ptr<CommitLog> commit_log;
    std::unique_ptr<ProcArray> proc_array;
    LockManager lock_manager;
    SerializableConflictManager ssi_manager;
    XidWaiter xid_waiter;
    std::unique_ptr<CompletionJournalPort> completion_journal;
    std::shared_ptr<std::shared_mutex> completion_gate{std::make_shared<std::shared_mutex>()};
    std::mutex abandoned_recovery_mutex;
};

auto TransactionManager::make_xid_waiter(LockManager& lock_manager) noexcept -> XidWaiter {
    return XidWaiter{lock_manager};
}

CheckpointCompletionBarrier::CheckpointCompletionBarrier(std::unique_ptr<Inner> inner) noexcept
    : inner_{std::move(inner)} {}

CheckpointCompletionBarrier::~CheckpointCompletionBarrier() = default;
CheckpointCompletionBarrier::CheckpointCompletionBarrier(
    CheckpointCompletionBarrier&& other
) noexcept = default;
auto CheckpointCompletionBarrier::operator=(CheckpointCompletionBarrier&& other) noexcept
    -> CheckpointCompletionBarrier& = default;

TransactionManager::TransactionManager()
    : TransactionManager{LockManagerOptions{}} {}

TransactionManager::TransactionManager(const LockManagerOptions& lock_options)
    : inner_{std::make_unique<Inner>(
          std::make_shared<CommitLog>(),
          std::make_unique<ProcArray>(),
          lock_options,
          nullptr
      )} {}

TransactionManager::TransactionManager(std::unique_ptr<Inner> inner) noexcept
    : inner_{std::move(inner)} {}

TransactionManager::~TransactionManager() = default;

auto TransactionManager::with_completion_journal(
    std::unique_ptr<CompletionJournalPort> completion_journal
) -> base::Result<std::unique_ptr<TransactionManager>> {
    if (completion_journal == nullptr) {
        return base::fail(manager_error(
            base::ErrorCode::kInvalidArgument,
            "transaction completion journal cannot be null",
            "construct"
        ));
    }
    return std::unique_ptr<TransactionManager>{new TransactionManager{std::make_unique<Inner>(
        std::make_shared<CommitLog>(),
        std::make_unique<ProcArray>(),
        LockManagerOptions{},
        std::move(completion_journal)
    )}};
}

auto TransactionManager::from_startup_state(
    TransactionStartupState startup_state,
    std::unique_ptr<CompletionJournalPort> completion_journal
) -> base::Result<std::unique_ptr<TransactionManager>> {
    if (startup_state.commit_log_ == nullptr) {
        return base::fail(manager_error(
            base::ErrorCode::kInvalidArgument,
            "transaction startup state has already been consumed",
            "construct from startup state"
        ));
    }
    auto proc_array = ProcArray::from_horizon(startup_state.horizon_);
    if (!proc_array) {
        return base::fail(
            std::move(proc_array)
                .error()
                .with_context("transaction/manager", "construct from startup state")
        );
    }
    return std::unique_ptr<TransactionManager>{new TransactionManager{std::make_unique<Inner>(
        std::move(startup_state.commit_log_),
        std::move(*proc_array),
        LockManagerOptions{},
        std::move(completion_journal)
    )}};
}

auto TransactionManager::commit_log() const noexcept -> const CommitLog& {
    return *inner_->commit_log;
}

auto TransactionManager::proc_array() const noexcept -> const ProcArray& {
    return *inner_->proc_array;
}

auto TransactionManager::snapshot_coordinator(ProcSlotLease& lease) noexcept
    -> SnapshotCoordinator {
    return SnapshotCoordinator{*inner_->proc_array, lease};
}

auto TransactionManager::lock_manager() noexcept -> LockManager& {
    return inner_->lock_manager;
}

auto TransactionManager::lock_manager() const noexcept -> const LockManager& {
    return inner_->lock_manager;
}

auto TransactionManager::ssi_manager() noexcept -> SerializableConflictManager& {
    return inner_->ssi_manager;
}

auto TransactionManager::ssi_manager() const noexcept -> const SerializableConflictManager& {
    return inner_->ssi_manager;
}

auto TransactionManager::xid_waiter() noexcept -> XidWaiter& {
    return inner_->xid_waiter;
}

auto TransactionManager::xid_waiter() const noexcept -> const XidWaiter& {
    return inner_->xid_waiter;
}

auto TransactionManager::xid_horizon() const -> XidHorizon {
    return inner_->proc_array->horizon();
}

auto TransactionManager::register_session() -> base::Result<ProcSlotLease> {
    return inner_->proc_array->register_session();
}

[[maybe_unused]] auto TransactionManager::assign_xid([[maybe_unused]] ProcSlotLease& lease)
    -> base::Result<base::Xid> {
    // LAB-S08.Q02:R21-BEGIN
    base::lab::missing_algorithm("S08.Q02:R21");
    // LAB-S08.Q02:R21-END
}

[[maybe_unused]] auto TransactionManager::complete(
    [[maybe_unused]] ProcSlotLease& lease,
    [[maybe_unused]] base::Xid expected_xid,
    [[maybe_unused]] TransactionStatus final_status
) -> base::Status {
    // LAB-S08.Q06:R16-BEGIN
    base::lab::missing_algorithm("S08.Q06:R16");
    // LAB-S08.Q06:R16-END
}

[[maybe_unused]] auto TransactionManager::complete_read_only(
    [[maybe_unused]] ProcSlotLease& lease,
    [[maybe_unused]] base::Xid expected_xid
) -> base::Status {
    // LAB-S08.Q06:R17-BEGIN
    base::lab::missing_algorithm("S08.Q06:R17");
    // LAB-S08.Q06:R17-END
}

[[maybe_unused]] auto TransactionManager::complete_unlogged(
    [[maybe_unused]] ProcSlotLease& lease,
    [[maybe_unused]] const base::Xid expected_xid,
    [[maybe_unused]] const TransactionStatus final_status
) -> base::Status {
    // LAB-S08.Q06:R18-BEGIN
    base::lab::missing_algorithm("S08.Q06:R18");
    // LAB-S08.Q06:R18-END
}

auto TransactionManager::rollback_abandoned_transactions() -> base::Result<std::size_t> {
    const std::lock_guard recovery_lock{inner_->abandoned_recovery_mutex};
    const auto abandoned = inner_->proc_array->abandoned_transactions();
    std::size_t completed = 0;
    for (const auto xid : abandoned) {
        if (auto status = detail::publish_abandoned_transaction_abort(
                inner_->completion_services(),
                inner_->ssi_manager,
                xid
            );
            !status) {
            return base::fail(std::move(status).error());
        }
        ++completed;
    }
    return completed;
}

[[maybe_unused]] auto TransactionManager::flush_commit_log() -> base::Status {
    // LAB-S08.Q06:R19-BEGIN
    base::lab::missing_algorithm("S08.Q06:R19");
    // LAB-S08.Q06:R19-END
}

[[maybe_unused]] auto TransactionManager::checkpoint_completion_barrier()
    -> CheckpointCompletionBarrier {
    // LAB-S08.Q06:R20-BEGIN
    base::lab::missing_algorithm("S08.Q06:R20");
    // LAB-S08.Q06:R20-END
}

}  // namespace db::transaction
