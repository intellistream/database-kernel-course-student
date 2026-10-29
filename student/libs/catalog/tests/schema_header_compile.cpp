#include <type_traits>

#include "db/catalog/schema.hpp"

static_assert(std::is_copy_constructible_v<db::catalog::Schema>);
static_assert(std::is_move_constructible_v<db::catalog::Schema>);
