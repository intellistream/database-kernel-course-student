#include <type_traits>

#include "db/access/btree_maintenance.hpp"

namespace db::access::header_compile {

template <typename Type>
concept ExposesImplementation = requires { typename Type::Impl; };

static_assert(sizeof(BtreeCleanupStats) == sizeof(std::size_t) * 3U);
static_assert(std::is_nothrow_move_constructible_v<BtreeCleanupBatch>);
static_assert(std::is_nothrow_move_assignable_v<BtreeCleanupBatch>);
static_assert(!ExposesImplementation<BtreeCleanupBatch>);

}  // namespace db::access::header_compile
