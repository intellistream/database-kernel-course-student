#include <type_traits>

#include "db/execution/row_count.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::RowCountSupport>);
static_assert(std::is_nothrow_move_constructible_v<db::execution::RowCountSupport>);
static_assert(std::is_nothrow_move_assignable_v<db::execution::RowCountSupport>);
