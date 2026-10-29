#include <memory>
#include <type_traits>

#include "db/execution/hash_join.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::HashJoinState>);
static_assert(!std::is_move_constructible_v<db::execution::HashJoinState>);
static_assert(std::is_destructible_v<db::execution::HashJoinState>);
