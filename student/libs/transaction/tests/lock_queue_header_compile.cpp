#include "lock_queue.hpp"

namespace {

[[maybe_unused]] void compile_lock_queue_header() {
    db::transaction::detail::LockState state;
    db::transaction::detail::GrantedLockOwners granted;
    db::transaction::detail::WaitingLockOwners owners;
    db::transaction::detail::grant_compatible_waiters(state, granted, owners);
}

}  // namespace
