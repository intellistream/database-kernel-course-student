#include <type_traits>

#include "db/execution/plan_state.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::PlanState>);
static_assert(!std::is_move_constructible_v<db::execution::PlanState>);
