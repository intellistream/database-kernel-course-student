#include <type_traits>

#include "lock_manager_state.hpp"

static_assert(!std::is_copy_constructible_v<db::transaction::detail::LockRequest>);
