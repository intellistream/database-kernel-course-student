#include <type_traits>

#include "db/execution/aggregate.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::AggregateState>);
static_assert(!std::is_move_constructible_v<db::execution::AggregateState>);
