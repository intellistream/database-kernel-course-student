#include <type_traits>

#include "db/storage/free_space_map.hpp"

namespace {

template <typename Type>
concept ExposesFreeSpaceMapImplementation = requires { typename Type::Impl; };

static_assert(!std::is_copy_constructible_v<db::storage::FreeSpaceMap>);
static_assert(std::is_nothrow_move_constructible_v<db::storage::FreeSpaceMap>);
static_assert(std::is_nothrow_move_assignable_v<db::storage::FreeSpaceMap>);
static_assert(db::storage::kMaximumPageFreeSpace < db::storage::kPageSize);
static_assert(!ExposesFreeSpaceMapImplementation<db::storage::FreeSpaceMap>);

}  // namespace
