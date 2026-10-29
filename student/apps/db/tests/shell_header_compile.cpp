#include <type_traits>

#include "db/shell.hpp"

static_assert(std::is_move_constructible_v<db::shell::Shell>);
static_assert(std::is_nothrow_move_constructible_v<db::shell::Shell>);
static_assert(!std::is_copy_constructible_v<db::shell::Shell>);
static_assert(!std::is_move_assignable_v<db::shell::Shell>);
