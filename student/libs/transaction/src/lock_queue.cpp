#include "lock_queue.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include "db/base/lab_missing.hpp"

#include "lock_mode_policy.hpp"

namespace db::transaction::detail {
[[maybe_unused]] auto lock_request_compatible_with_holders(
    [[maybe_unused]] const LockState& state,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const LockTargetKind kind,
    [[maybe_unused]] const std::uint8_t mode
) -> bool {
    // LAB-S08.Q04:R11-BEGIN
    base::lab::missing_algorithm("S08.Q04:R11");
    // LAB-S08.Q04:R11-END
}

[[maybe_unused]] auto lock_state_is_empty([[maybe_unused]] const LockState& state) noexcept
    -> bool {
    // LAB-S08.Q04:R12-BEGIN
    base::lab::missing_algorithm("S08.Q04:R12");
    // LAB-S08.Q04:R12-END
}

[[maybe_unused]] void grant_compatible_waiters(
    [[maybe_unused]] LockState& state,
    [[maybe_unused]] GrantedLockOwners& granted_by_owner,
    [[maybe_unused]] WaitingLockOwners& waiting_by_owner
) {
    // LAB-S08.Q04:R13-BEGIN
    base::lab::missing_algorithm("S08.Q04:R13");
    // LAB-S08.Q04:R13-END
}

[[maybe_unused]] void withdraw_lock_request(
    [[maybe_unused]] LockState& state,
    [[maybe_unused]] GrantedLockOwners& granted_by_owner,
    [[maybe_unused]] WaitingLockOwners& waiting_by_owner,
    [[maybe_unused]] const std::shared_ptr<LockRequest>& request
) {
    // LAB-S08.Q04:R14-BEGIN
    base::lab::missing_algorithm("S08.Q04:R14");
    // LAB-S08.Q04:R14-END
}

[[maybe_unused]] void cancel_lock_request(
    [[maybe_unused]] LockState& state,
    [[maybe_unused]] GrantedLockOwners& granted_by_owner,
    [[maybe_unused]] WaitingLockOwners& waiting_by_owner,
    [[maybe_unused]] const std::shared_ptr<LockRequest>& request
) {
    // LAB-S08.Q04:R15-BEGIN
    base::lab::missing_algorithm("S08.Q04:R15");
    // LAB-S08.Q04:R15-END
}

[[maybe_unused]] auto release_lock_owner(
    [[maybe_unused]] LockTable& locks,
    [[maybe_unused]] GrantedLockOwners& granted_by_owner,
    [[maybe_unused]] WaitingLockOwners& waiting_by_owner,
    [[maybe_unused]] const base::Xid xid
) -> bool {
    // LAB-S08.Q04:R16-BEGIN
    base::lab::missing_algorithm("S08.Q04:R16");
    // LAB-S08.Q04:R16-END
}

[[maybe_unused]] void cancel_all_lock_waiters(
    [[maybe_unused]] LockTable& locks,
    [[maybe_unused]] GrantedLockOwners& granted_by_owner,
    [[maybe_unused]] WaitingLockOwners& waiting_by_owner
) {
    // LAB-S08.Q04:R17-BEGIN
    base::lab::missing_algorithm("S08.Q04:R17");
    // LAB-S08.Q04:R17-END
}

}  // namespace db::transaction::detail
