#include <type_traits>

#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"

namespace {

template <typename Type>
concept ExposesImplementation = requires { typename Type::Impl; };

template <typename Type>
concept ExposesState = requires { typename Type::State; };

static_assert(!std::is_copy_constructible_v<db::access::BtreeRelation>);
static_assert(std::is_nothrow_move_constructible_v<db::access::BtreeRelation>);
static_assert(std::is_nothrow_move_assignable_v<db::access::BtreeRelation>);
static_assert(!std::is_copy_constructible_v<db::access::BtreeScan>);
static_assert(std::is_nothrow_move_constructible_v<db::access::BtreeScan>);
static_assert(std::is_nothrow_move_assignable_v<db::access::BtreeScan>);
static_assert(std::is_trivially_copyable_v<db::access::BtreeScanCandidate>);
static_assert(!ExposesImplementation<db::access::BtreeScan>);
static_assert(!ExposesState<db::access::BtreeRelation>);

}  // namespace
