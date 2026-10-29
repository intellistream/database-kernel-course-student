#include "db/transaction/proc_array.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "proc_array_state.hpp"
#include "xid_assignment.hpp"

namespace db::transaction {
namespace {

constexpr auto kInactiveIndex = detail::kInactiveProcSlotIndex;

using detail::proc_array_error;

}  // namespace

ProcArray::ProcArray()
    : state_{std::make_shared<detail::ProcArrayState>(
          std::make_unique<XidGenerator>(),
          base::kInvalidXid
      )} {}

ProcArray::ProcArray(std::shared_ptr<detail::ProcArrayState> state) noexcept
    : state_{std::move(state)} {}

[[maybe_unused]] auto ProcArray::from_horizon([[maybe_unused]] XidHorizon horizon)
    -> base::Result<std::unique_ptr<ProcArray>> {
    // LAB-S08.Q02:R14-BEGIN
    base::lab::missing_algorithm("S08.Q02:R14");
    // LAB-S08.Q02:R14-END
}

[[maybe_unused]] auto ProcArray::register_session() -> base::Result<ProcSlotLease> {
    // LAB-S08.Q02:R15-BEGIN
    base::lab::missing_algorithm("S08.Q02:R15");
    // LAB-S08.Q02:R15-END
}

[[maybe_unused]] auto ProcArray::assign_xid([[maybe_unused]] ProcSlotLease& lease)
    -> base::Result<base::Xid> {
    // LAB-S08.Q02:R16-BEGIN
    base::lab::missing_algorithm("S08.Q02:R16");
    // LAB-S08.Q02:R16-END
}

[[maybe_unused]] auto ProcArray::assign_xid_prepared(
    [[maybe_unused]] ProcSlotLease& lease,
    [[maybe_unused]] detail::XidAssignmentPreparation& preparation
) -> base::Result<base::Xid> {
    // LAB-S08.Q02:R17-BEGIN
    base::lab::missing_algorithm("S08.Q02:R17");
    // LAB-S08.Q02:R17-END
}

[[maybe_unused]] auto ProcArray::validate_active_xid(
    [[maybe_unused]] const ProcSlotLease& lease,
    [[maybe_unused]] base::Xid expected_xid
) const -> base::Status {
    // LAB-S08.Q02:R18-BEGIN
    base::lab::missing_algorithm("S08.Q02:R18");
    // LAB-S08.Q02:R18-END
}

auto ProcArray::abandoned_transactions() const -> std::vector<base::Xid> {
    const std::shared_lock lock{state_->mutex};
    std::vector<base::Xid> abandoned;
    for (const auto slot_index : state_->active_slots) {
        const auto& entry = state_->slots[slot_index];
        if (entry.abandoned && entry.slot->has_active_transaction()) {
            abandoned.push_back(entry.slot->xid());
        }
    }
    std::ranges::sort(abandoned, {}, &base::Xid::value);
    return abandoned;
}

auto ProcArray::publish_abandoned_completion(base::Xid expected_xid) -> base::Status {
    if (!is_normal_xid(expected_xid)) {
        return base::fail(proc_array_error(
            base::ErrorCode::kInvalidArgument,
            "abandoned completion requires a normal expected XID",
            "publish abandoned completion"
        ));
    }
    const std::unique_lock lock{state_->mutex};
    for (const auto slot_index : state_->active_slots) {
        auto& entry = state_->slots[slot_index];
        if (!entry.abandoned || entry.slot->xid() != expected_xid) {
            continue;
        }
        auto status = clear_slot_locked(*entry.slot, expected_xid);
        if (!status) {
            return status;
        }
        state_->unregister_slot_locked(slot_index);
        return {};
    }
    return base::fail(proc_array_error(
        base::ErrorCode::kNotFound,
        "abandoned transaction is no longer registered",
        "publish abandoned completion"
    ));
}

auto ProcArray::horizon() const -> XidHorizon {
    const std::shared_lock lock{state_->mutex};
    return XidHorizon{
        .next_xid = state_->generator->next_xid(),
        .latest_completed_xid = state_->latest_completed_xid
    };
}

auto ProcArray::has_at_least_active_transactions(std::size_t minimum) const -> bool {
    if (minimum == 0) {
        return true;
    }
    const std::shared_lock lock{state_->mutex};
    std::size_t active = 0;
    for (const auto slot_index : state_->active_slots) {
        if (state_->slots[slot_index].slot->has_active_transaction()) {
            ++active;
            if (active >= minimum) {
                return true;
            }
        }
    }
    return false;
}

auto ProcArray::registered_session_count() const -> std::size_t {
    const std::shared_lock lock{state_->mutex};
    return state_->active_slots.size();
}

auto ProcArray::slot_capacity() const -> std::size_t {
    const std::shared_lock lock{state_->mutex};
    return state_->slots.size();
}

auto ProcArray::completion_group_stats() const noexcept -> CompletionGroupStats {
    return CompletionGroupStats{
        .leaders = state_->completion_group_leaders.load(std::memory_order_relaxed),
        .members = state_->completion_group_members.load(std::memory_order_relaxed)
    };
}

[[maybe_unused]] auto
ProcArray::validate_lease_locked([[maybe_unused]] const ProcSlotLease& lease) const
    -> base::Status {
    // LAB-S08.Q02:R19-BEGIN
    base::lab::missing_algorithm("S08.Q02:R19");
    // LAB-S08.Q02:R19-END
}

}  // namespace db::transaction
