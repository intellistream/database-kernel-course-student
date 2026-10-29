#include <type_traits>

#include "id_allocator.hpp"

static_assert(!std::is_copy_constructible_v<db::catalog::CatalogIdAllocator>);
static_assert(std::is_nothrow_move_constructible_v<db::catalog::CatalogIdAllocator>);

namespace {

[[maybe_unused]] const db::catalog::CatalogIdState kDefaultState{};

}  // namespace
