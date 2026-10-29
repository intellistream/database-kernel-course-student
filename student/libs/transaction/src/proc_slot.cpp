#include "db/transaction/proc_slot.hpp"

#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction {
namespace {

[[maybe_unused]] auto slot_error(base::ErrorCode code, std::string message, std::string operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "transaction/proc-slot",
        std::move(operation)
    );
}

}  // namespace

[[maybe_unused]] auto ProcSlot::create([[maybe_unused]] base::SlotId slot_id)
    -> base::Result<std::unique_ptr<ProcSlot>> {
    // LAB-S08.Q02:R05-BEGIN
    base::lab::missing_algorithm("S08.Q02:R05");
    // LAB-S08.Q02:R05-END
}

auto ProcSlot::slot_id() const noexcept -> base::SlotId {
    return slot_id_;
}

auto ProcSlot::xid() const noexcept -> base::Xid {
    return base::Xid{xid_.load(std::memory_order_acquire)};
}

auto ProcSlot::snapshot_xmin() const noexcept -> base::Xid {
    return base::Xid{snapshot_xmin_.load(std::memory_order_acquire)};
}

auto ProcSlot::has_active_transaction() const noexcept -> bool {
    return xid() != base::kInvalidXid;
}

[[maybe_unused]] auto ProcSlot::assign_xid([[maybe_unused]] base::Xid xid) -> base::Status {
    // LAB-S08.Q02:R06-BEGIN
    base::lab::missing_algorithm("S08.Q02:R06");
    // LAB-S08.Q02:R06-END
}

[[maybe_unused]] auto ProcSlot::clear_xid([[maybe_unused]] base::Xid expected_xid) -> base::Status {
    // LAB-S08.Q02:R07-BEGIN
    base::lab::missing_algorithm("S08.Q02:R07");
    // LAB-S08.Q02:R07-END
}

[[maybe_unused]] auto ProcSlot::advertise_snapshot_xmin([[maybe_unused]] base::Xid xmin) noexcept
    -> base::Xid {
    // LAB-S08.Q02:R08-BEGIN
    base::lab::missing_algorithm("S08.Q02:R08");
    // LAB-S08.Q02:R08-END
}

[[maybe_unused]] void ProcSlot::reset_snapshot_xmin() noexcept {
    // LAB-S08.Q02:R09-BEGIN
    base::lab::missing_algorithm("S08.Q02:R09");
    // LAB-S08.Q02:R09-END
}

ProcSlot::ProcSlot(base::SlotId slot_id) noexcept
    : slot_id_{slot_id} {}

}  // namespace db::transaction
