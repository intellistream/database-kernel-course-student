#include <type_traits>

#include "db/execution/topn.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::TopNState>);
static_assert(!std::is_move_constructible_v<db::execution::TopNState>);
