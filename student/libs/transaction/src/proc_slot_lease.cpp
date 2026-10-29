#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/proc_array.hpp"

#include "proc_array_state.hpp"

namespace db::transaction {

ProcSlotLease::ProcSlotLease(
    std::shared_ptr<detail::ProcArrayState> state,
    std::size_t slot_index,
    std::uint64_t generation
) noexcept
    : state_{std::move(state)},
      slot_{state_->slots[slot_index].slot.get()},
      slot_index_{slot_index},
      generation_{generation} {}

ProcSlotLease::~ProcSlotLease() {
    if (valid()) {
        auto status = close();
        if (!status) {
            mark_live_transaction_abandoned();
        }
    }
}

ProcSlotLease::ProcSlotLease(ProcSlotLease&& other) noexcept
    : state_{std::move(other.state_)},
      slot_{std::exchange(other.slot_, nullptr)},
      slot_index_{std::exchange(other.slot_index_, 0)},
      generation_{std::exchange(other.generation_, 0)} {}

auto ProcSlotLease::operator=(ProcSlotLease&& other) noexcept -> ProcSlotLease& {
    if (this == &other) {
        return *this;
    }
    std::swap(state_, other.state_);
    std::swap(slot_, other.slot_);
    std::swap(slot_index_, other.slot_index_);
    std::swap(generation_, other.generation_);
    return *this;
}

auto ProcSlotLease::valid() const noexcept -> bool {
    return state_ != nullptr && slot_ != nullptr && generation_ != 0;
}

auto ProcSlotLease::slot() const noexcept -> const ProcSlot* {
    return valid() ? slot_ : nullptr;
}

auto ProcSlotLease::close() -> base::Status {
    if (!valid()) {
        return base::fail(
            detail::proc_array_error(
                base::ErrorCode::kInvalidArgument,
                "session lease is not active",
                "unregister session"
            )
        );
    }

    // Keep the shared state alive until after lock destruction even when the
    // ProcArray facade has already been destroyed.
    const auto state = state_;
    const std::unique_lock lock{state->mutex};
    if (slot_index_ >= state->slots.size()) {
        return base::fail(
            detail::proc_array_error(
                base::ErrorCode::kInvariantViolation,
                "session lease index is outside ProcArray",
                "unregister session"
            )
        );
    }
    auto& entry = state->slots[slot_index_];
    if (!entry.registered || entry.abandoned || entry.generation != generation_
        || entry.slot.get() != slot_) {
        return base::fail(
            detail::proc_array_error(
                base::ErrorCode::kTransactionState,
                "session lease is stale or already unregistered",
                "unregister session"
            )
        );
    }
    if (slot_->has_active_transaction()) {
        return base::fail(
            detail::proc_array_error(
                base::ErrorCode::kTransactionState,
                "cannot unregister a session with an active XID",
                "unregister session"
            )
        );
    }
    if (entry.active_index >= state->active_slots.size()
        || state->active_slots[entry.active_index] != slot_index_) {
        return base::fail(
            detail::proc_array_error(
                base::ErrorCode::kInvariantViolation,
                "registered session is absent from the dense scan set",
                "unregister session"
            )
        );
    }

    state->unregister_slot_locked(slot_index_);
    abandon();
    return {};
}

void ProcSlotLease::abandon() noexcept {
    state_.reset();
    slot_ = nullptr;
    slot_index_ = 0;
    generation_ = 0;
}

void ProcSlotLease::mark_live_transaction_abandoned() noexcept {
    if (!valid()) {
        return;
    }
    const auto state = state_;
    const std::unique_lock lock{state->mutex};
    if (slot_index_ < state->slots.size()) {
        auto& entry = state->slots[slot_index_];
        if (entry.registered && entry.generation == generation_ && entry.slot.get() == slot_
            && slot_->has_active_transaction()) {
            entry.abandoned = true;
        }
    }
    abandon();
}

}  // namespace db::transaction
