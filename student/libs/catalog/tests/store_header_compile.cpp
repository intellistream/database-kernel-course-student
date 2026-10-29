#include <type_traits>

#include "db/catalog/store.hpp"

namespace {

static_assert(!std::is_copy_constructible_v<db::catalog::CatalogStore>);
static_assert(std::is_nothrow_move_constructible_v<db::catalog::CatalogStore>);
static_assert(std::is_nothrow_move_assignable_v<db::catalog::CatalogStore>);

}  // namespace
