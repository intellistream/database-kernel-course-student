#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::transaction {

namespace detail {
class ProcArrayState;
}  // namespace detail

class ProcArray;
class ProcSlotLease;
class XidGenerator;

// Stable per-session publication object, analogous to the XID fields of a
// PGPROC. ProcArray owns and reuses these objects; transaction
// completion only clears fields and never moves the slot.
class ProcSlot final {
public:
    ~ProcSlot() = default;
    ProcSlot(const ProcSlot&) = delete;
    auto operator=(const ProcSlot&) -> ProcSlot& = delete;
    ProcSlot(ProcSlot&&) = delete;
    auto operator=(ProcSlot&&) -> ProcSlot& = delete;

    static auto create(base::SlotId slot_id) -> base::Result<std::unique_ptr<ProcSlot>>;

    auto slot_id() const noexcept -> base::SlotId;
    auto xid() const noexcept -> base::Xid;
    auto snapshot_xmin() const noexcept -> base::Xid;
    auto has_active_transaction() const noexcept -> bool;

    auto assign_xid(base::Xid xid) -> base::Status;
    auto clear_xid(base::Xid expected_xid) -> base::Status;

private:
    friend class ProcArray;
    friend class ProcSlotLease;
    friend class XidGenerator;
    friend class detail::ProcArrayState;

    explicit ProcSlot(base::SlotId slot_id) noexcept;

    auto advertise_snapshot_xmin(base::Xid xmin) noexcept -> base::Xid;
    void reset_snapshot_xmin() noexcept;

    base::SlotId slot_id_;
    // Serializes lifecycle transitions for callers that share a session by
    // mistake while retaining atomic publication for ProcArray readers.
    std::mutex assignment_mutex_;
    std::atomic<std::uint32_t> xid_{base::kInvalidXid.value()};
    std::atomic<std::uint32_t> snapshot_xmin_{base::kInvalidXid.value()};
    // Intrusive completion-group state. Stable ProcSlot allocation lets a
    // contended leader clear several XIDs with one ProcArray exclusive lock.
    std::atomic<bool> completion_group_member_{false};
    std::atomic<ProcSlot*> completion_group_next_{nullptr};
    std::atomic<std::uint32_t> completion_group_xid_{base::kInvalidXid.value()};
    // 0=pending/not used, 1=success, 2=invariant failure.
    std::atomic<std::uint8_t> completion_group_result_{0};
};

}  // namespace db::transaction
