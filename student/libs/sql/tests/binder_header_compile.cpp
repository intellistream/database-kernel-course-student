#include <type_traits>

#include "db/sql/binder.hpp"

static_assert(std::is_move_constructible_v<db::sql::BoundScript>);
static_assert(!std::is_copy_constructible_v<db::sql::BoundScript>);
