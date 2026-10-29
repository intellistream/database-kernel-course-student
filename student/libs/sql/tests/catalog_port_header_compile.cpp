#include <type_traits>

#include "db/sql/catalog_port.hpp"

static_assert(std::has_virtual_destructor_v<db::sql::CatalogReadPort>);
static_assert(!std::is_copy_constructible_v<db::sql::CatalogReadPort>);
