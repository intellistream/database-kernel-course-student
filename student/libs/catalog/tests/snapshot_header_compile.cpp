#include <type_traits>

#include "db/catalog/snapshot.hpp"

static_assert(std::is_copy_constructible_v<db::catalog::CatalogSnapshot>);
static_assert(std::is_move_constructible_v<db::catalog::CatalogSnapshot>);
