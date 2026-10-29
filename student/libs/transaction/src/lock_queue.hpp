#pragma once

#include <memory>

#include "db/base/identifier.hpp"

#include "lock_manager_state.hpp"

namespace db::transaction::detail {

// Every operation requires the owning LockManager mutex. These functions are
// the only boundary that mutates request terminal state and queue membership.
auto lock_state_is_empty(const LockState& state) noexcept -> bool;
auto lock_request_compatible_with_holders(
    const LockState& state,
    base::Xid xid,
    LockTargetKind kind,
    std::uint8_t mode
) -> bool;
void grant_compatible_waiters(
    LockState& state,
    GrantedLockOwners& granted_by_owner,
    WaitingLockOwners& waiting_by_owner
);
void withdraw_lock_request(
    LockState& state,
    GrantedLockOwners& granted_by_owner,
    WaitingLockOwners& waiting_by_owner,
    const std::shared_ptr<LockRequest>& request
);
void cancel_lock_request(
    LockState& state,
    GrantedLockOwners& granted_by_owner,
    WaitingLockOwners& waiting_by_owner,
    const std::shared_ptr<LockRequest>& request
);
auto release_lock_owner(
    LockTable& locks,
    GrantedLockOwners& granted_by_owner,
    WaitingLockOwners& waiting_by_owner,
    base::Xid xid
) -> bool;
void cancel_all_lock_waiters(
    LockTable& locks,
    GrantedLockOwners& granted_by_owner,
    WaitingLockOwners& waiting_by_owner
);

}  // namespace db::transaction::detail
