#include <type_traits>

#include "db/execution/sort_support.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::SortSupport>);
static_assert(std::is_nothrow_move_constructible_v<db::execution::SortSupport>);
static_assert(std::is_nothrow_move_assignable_v<db::execution::SortSupport>);
