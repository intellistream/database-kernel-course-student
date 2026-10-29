#include <type_traits>

#include "db/execution/nested_loop.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::NestedLoopState>);
static_assert(!std::is_move_constructible_v<db::execution::NestedLoopState>);
