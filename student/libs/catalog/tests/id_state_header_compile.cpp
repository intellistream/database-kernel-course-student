#include <type_traits>

#include "db/catalog/id_state.hpp"

static_assert(std::is_trivially_copyable_v<db::catalog::CatalogIdState>);
