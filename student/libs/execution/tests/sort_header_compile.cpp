#include <type_traits>

#include "db/execution/sort.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::SortState>);
static_assert(!std::is_move_constructible_v<db::execution::SortState>);
