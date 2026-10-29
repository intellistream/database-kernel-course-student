#include <type_traits>

#include "db/execution/exec_context.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::ExecContext>);
static_assert(std::is_nothrow_move_constructible_v<db::execution::ExecContext>);
static_assert(std::is_nothrow_move_assignable_v<db::execution::ExecContext>);
