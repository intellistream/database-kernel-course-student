#include <type_traits>

#include "lock_mode_policy.hpp"

static_assert(std::is_enum_v<db::transaction::detail::LockTargetKind>);
