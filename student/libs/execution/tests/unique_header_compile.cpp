#include <type_traits>

#include "db/execution/unique.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::UniqueState>);
static_assert(!std::is_move_constructible_v<db::execution::UniqueState>);
