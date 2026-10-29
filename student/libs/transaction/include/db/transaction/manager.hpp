#pragma once

#include <chrono>
#include <cstddef>
#include <memory>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/completion_journal.hpp"
#include "db/transaction/lock_manager.hpp"
#include "db/transaction/proc_array.hpp"
#include "db/transaction/ssi.hpp"
#include "db/transaction/xid_waiter.hpp"

namespace db::transaction {

class SnapshotCoordinator;

inline constexpr auto kTransactionGroupCommitDelay = std::chrono::microseconds{250};
inline constexpr std::size_t kTransactionGroupCommitSiblings = 5;

// Exclusive checkpoint-side barrier. While held, no transaction may cross its
// completion WAL/CLOG/ProcArray publication interval.
class CheckpointCompletionBarrier final {
public:
    ~CheckpointCompletionBarrier();
    CheckpointCompletionBarrier(const CheckpointCompletionBarrier&) = delete;
    auto operator=(const CheckpointCompletionBarrier&) -> CheckpointCompletionBarrier& = delete;
    CheckpointCompletionBarrier(CheckpointCompletionBarrier&& other) noexcept;
    auto operator=(CheckpointCompletionBarrier&& other) noexcept -> CheckpointCompletionBarrier&;

private:
    friend class TransactionManager;

    struct Inner;

    explicit CheckpointCompletionBarrier(std::unique_ptr<Inner> inner) noexcept;

    std::unique_ptr<Inner> inner_;
};

// Move-only proof that one exact CLOG and XID horizon are ready for steady
// state. Clean startup trusts the already-validated shutdown publication;
// crash startup additionally requires recovery to have resolved every earlier
// allocation. The composition root must consume the proof immediately and
// must not mutate the shared CLOG independently after handoff.
class TransactionStartupState final {
public:
    static auto from_clean_state(std::shared_ptr<CommitLog> commit_log, XidHorizon horizon)
        -> base::Result<TransactionStartupState>;
    static auto from_recovered_state(std::shared_ptr<CommitLog> commit_log, XidHorizon horizon)
        -> base::Result<TransactionStartupState>;

    ~TransactionStartupState() = default;
    TransactionStartupState(const TransactionStartupState&) = delete;
    auto operator=(const TransactionStartupState&) -> TransactionStartupState& = delete;
    TransactionStartupState(TransactionStartupState&& other) noexcept = default;
    auto operator=(TransactionStartupState&& other) noexcept -> TransactionStartupState& = default;

private:
    friend class TransactionManager;

    TransactionStartupState(std::shared_ptr<CommitLog> commit_log, XidHorizon horizon) noexcept;

    std::shared_ptr<CommitLog> commit_log_;
    XidHorizon horizon_;
};

// Process-wide transaction publication services. Session-local transaction
// state remains separate and holds one ProcSlotLease from this manager.
class TransactionManager final {
public:
    TransactionManager();
    explicit TransactionManager(const LockManagerOptions& lock_options);
    ~TransactionManager();
    TransactionManager(const TransactionManager&) = delete;
    auto operator=(const TransactionManager&) -> TransactionManager& = delete;
    TransactionManager(TransactionManager&&) = delete;
    auto operator=(TransactionManager&&) -> TransactionManager& = delete;

    static auto with_completion_journal(std::unique_ptr<CompletionJournalPort> completion_journal)
        -> base::Result<std::unique_ptr<TransactionManager>>;
    // A fuzzy checkpoint or live ProcArray horizon cannot be supplied here;
    // callers must first mint the matching clean or recovered startup proof.
    static auto from_startup_state(
        TransactionStartupState startup_state,
        std::unique_ptr<CompletionJournalPort> completion_journal = {}
    ) -> base::Result<std::unique_ptr<TransactionManager>>;

    // CLOG is observable but mutable publication remains manager-owned so it
    // cannot bypass ProcArray clearing and transaction-tag release.
    auto commit_log() const noexcept -> const CommitLog&;
    // ProcArray mutation remains manager-owned so callers cannot publish an
    // XID without its matching CLOG allocation and transaction tag.
    auto proc_array() const noexcept -> const ProcArray&;
    auto snapshot_coordinator(ProcSlotLease& lease) noexcept -> SnapshotCoordinator;
    auto lock_manager() noexcept -> LockManager&;
    auto lock_manager() const noexcept -> const LockManager&;
    auto ssi_manager() noexcept -> SerializableConflictManager&;
    auto ssi_manager() const noexcept -> const SerializableConflictManager&;
    auto xid_waiter() noexcept -> XidWaiter&;
    auto xid_waiter() const noexcept -> const XidWaiter&;
    auto xid_horizon() const -> XidHorizon;

    auto register_session() -> base::Result<ProcSlotLease>;
    auto assign_xid(ProcSlotLease& lease) -> base::Result<base::Xid>;

    // Commit: recovery-journal append + durable flush, then CLOG final, slot
    // clear, and lock release. Abort uses the same suffix without an immediate
    // journal flush because recovery aborts an unrecorded allocation.
    auto complete(ProcSlotLease& lease, base::Xid expected_xid, TransactionStatus final_status)
        -> base::Status;

    // Proven read-only commits have no persistent state for recovery to
    // reproduce, so they publish CLOG/ProcArray without a completion WAL
    // record. Callers that cannot prove this must use complete().
    auto complete_read_only(ProcSlotLease& lease, base::Xid expected_xid) -> base::Status;

    // Publishes a transaction whose data images were synchronously persisted
    // by a caller-owned protocol. It emits no transaction WAL. A committed
    // outcome fsyncs CLOG before returning; an aborted outcome needs no
    // immediate flush because startup treats an unrecorded allocation as
    // aborted.
    auto
    complete_unlogged(ProcSlotLease& lease, base::Xid expected_xid, TransactionStatus final_status)
        -> base::Status;

    // Abort live transactions whose ProcSlotLease owner disappeared. Each
    // successful cleanup publishes abort before removing the stable slot.
    auto rollback_abandoned_transactions() -> base::Result<std::size_t>;

    auto flush_commit_log() -> base::Status;
    auto checkpoint_completion_barrier() -> CheckpointCompletionBarrier;

private:
    struct Inner;

    static auto make_xid_waiter(LockManager& lock_manager) noexcept -> XidWaiter;
    explicit TransactionManager(std::unique_ptr<Inner> inner) noexcept;

    std::unique_ptr<Inner> inner_;
};

}  // namespace db::transaction
