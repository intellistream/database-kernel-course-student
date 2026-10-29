#include "db/transaction/xid_waiter.hpp"

#include "db/base/lab_missing.hpp"
#include "db/transaction/lock_manager.hpp"

namespace db::transaction {
XidWaiter::XidWaiter(LockManager& lock_manager) noexcept
    : lock_manager_{&lock_manager} {}

[[maybe_unused]] auto XidWaiter::wait_for(
    [[maybe_unused]] const base::Xid blocking_xid,
    [[maybe_unused]] const base::Xid waiter_xid
) const -> base::Status {
    // LAB-S08.Q08:R01-BEGIN
    base::lab::missing_algorithm("S08.Q08:R01");
    // LAB-S08.Q08:R01-END
}

auto XidWaiter::stats() const -> XidWaiterStats {
    const auto lock_stats = lock_manager_->stats();
    return XidWaiterStats{
        .blocking_transactions = lock_stats.transaction_wait_targets,
        .waiting_transactions = lock_stats.transaction_waiting_requests,
    };
}

}  // namespace db::transaction
