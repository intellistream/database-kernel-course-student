#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/proc_slot.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction {

namespace detail {
class ProcArrayState;
class TransactionCompletionAccess;
class XidAssignmentPreparation;
}  // namespace detail

struct XidHorizon final {
    base::Xid next_xid{kFirstNormalXid};
    base::Xid latest_completed_xid{base::kInvalidXid};

    auto operator==(const XidHorizon&) const -> bool = default;
};

struct Snapshot final {
    base::Xid xmin{kFirstNormalXid};
    base::Xid xmax{kFirstNormalXid};
    std::vector<base::Xid> in_progress;
    // Command IDs are session-local and are attached by SnapshotCoordinator.
    // ProcArray captures only the transaction-set portion and leaves this zero.
    std::uint32_t command_id{0};

    auto contains(base::Xid xid) const noexcept -> bool;
    auto operator==(const Snapshot&) const -> bool = default;
};

// Cached GetSnapshotData result. A cache is reusable until completion_count
// changes. Assignments do not invalidate it because they receive XIDs at or
// beyond the already-fixed xmax. Once the counter saturates, captures remain
// correct by disabling reuse rather than rejecting transaction completion.
struct RegisteredSnapshot final {
    Snapshot snapshot;
    base::Xid prune_xmin{kXidWrapBoundary};
    base::Xid owner_xid{base::kInvalidXid};
    std::uint64_t completion_count{0};

    auto operator==(const RegisteredSnapshot&) const -> bool = default;
};

struct SnapshotCapture final {
    RegisteredSnapshot registered_snapshot;
    bool reused{false};
};

struct CompletionGroupStats final {
    std::uint64_t leaders{0};
    std::uint64_t members{0};

    auto operator==(const CompletionGroupStats&) const -> bool = default;
};

// Move-only session membership. Destruction unregisters an idle session. A
// session destroyed with a live XID deliberately remains visible in ProcArray
// so disconnect cannot make an in-progress transaction appear completed.
class ProcSlotLease final {
public:
    ~ProcSlotLease();
    ProcSlotLease(const ProcSlotLease&) = delete;
    auto operator=(const ProcSlotLease&) -> ProcSlotLease& = delete;
    ProcSlotLease(ProcSlotLease&& other) noexcept;
    auto operator=(ProcSlotLease&& other) noexcept -> ProcSlotLease&;

    auto valid() const noexcept -> bool;
    // Returns a non-owning observer while this lease is active. The pointer is
    // null after close() or move and must not outlive the active lease.
    auto slot() const noexcept -> const ProcSlot*;
    auto close() -> base::Status;

private:
    friend class ProcArray;

    ProcSlotLease(
        std::shared_ptr<detail::ProcArrayState> state,
        std::size_t slot_index,
        std::uint64_t generation
    ) noexcept;

    void abandon() noexcept;
    void mark_live_transaction_abandoned() noexcept;

    std::shared_ptr<detail::ProcArrayState> state_;
    ProcSlot* slot_{nullptr};
    std::size_t slot_index_{0};
    std::uint64_t generation_{0};
};

class ProcArray final {
public:
    ProcArray();
    ~ProcArray() = default;
    ProcArray(const ProcArray&) = delete;
    auto operator=(const ProcArray&) -> ProcArray& = delete;
    ProcArray(ProcArray&&) = delete;
    auto operator=(ProcArray&&) -> ProcArray& = delete;

    static auto from_horizon(XidHorizon horizon) -> base::Result<std::unique_ptr<ProcArray>>;

    auto register_session() -> base::Result<ProcSlotLease>;
    auto assign_xid(ProcSlotLease& lease) -> base::Result<base::Xid>;
    auto validate_active_xid(const ProcSlotLease& lease, base::Xid expected_xid) const
        -> base::Status;

    // The final CLOG status and its required WAL record must be durable before
    // this method publishes the ProcArray visibility transition. A composed
    // manager may still release explicit locks and its transaction tag later.
    auto publish_completion(ProcSlotLease& lease, base::Xid expected_xid) -> base::Status;

    // A lease destroyed with a live XID marks its stable slot abandoned rather
    // than removing it from visibility. TransactionManager later publishes an
    // abort and calls this exact-XID cleanup path.
    auto abandoned_transactions() const -> std::vector<base::Xid>;
    auto publish_abandoned_completion(base::Xid expected_xid) -> base::Status;

    auto snapshot(base::Xid owner_xid = base::kInvalidXid) const -> base::Result<Snapshot>;
    auto snapshot_and_register(ProcSlotLease& lease, const RegisteredSnapshot* previous = nullptr)
        -> base::Result<SnapshotCapture>;
    auto clear_registered_snapshot(ProcSlotLease& lease, base::Xid expected_xid) -> base::Status;

    auto horizon() const -> XidHorizon;
    auto oldest_active_xmin() const -> base::Xid;
    auto has_at_least_active_transactions(std::size_t minimum) const -> bool;
    auto registered_session_count() const -> std::size_t;
    auto slot_capacity() const -> std::size_t;
    auto completion_group_stats() const noexcept -> CompletionGroupStats;

private:
    friend class detail::TransactionCompletionAccess;

    explicit ProcArray(std::shared_ptr<detail::ProcArrayState> state) noexcept;

    auto assign_xid_prepared(ProcSlotLease& lease, detail::XidAssignmentPreparation& preparation)
        -> base::Result<base::Xid>;
    auto publish_prevalidated_completion(ProcSlotLease& lease, base::Xid expected_xid)
        -> base::Status;
    auto validate_lease_locked(const ProcSlotLease& lease) const -> base::Status;
    auto clear_slot_locked(ProcSlot& slot, base::Xid expected_xid) -> base::Status;
    auto publish_group_completion(ProcSlotLease& lease, base::Xid expected_xid) -> base::Status;
    auto enqueue_group_completion(ProcSlotLease& lease, base::Xid expected_xid) -> base::Status;
    auto build_snapshot_locked(base::Xid owner_xid) const -> std::pair<Snapshot, base::Xid>;

    std::shared_ptr<detail::ProcArrayState> state_;
};

}  // namespace db::transaction
