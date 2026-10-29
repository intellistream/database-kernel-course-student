#include <type_traits>

#include "db/execution/limit.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::LimitState>);
static_assert(!std::is_move_constructible_v<db::execution::LimitState>);
