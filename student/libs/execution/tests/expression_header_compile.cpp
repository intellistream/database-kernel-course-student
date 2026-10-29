#include <type_traits>

#include "db/execution/expression.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::ExpressionProgram>);
static_assert(std::is_nothrow_move_constructible_v<db::execution::ExpressionProgram>);
static_assert(std::is_nothrow_move_assignable_v<db::execution::ExpressionProgram>);
