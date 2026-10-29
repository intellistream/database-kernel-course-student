#include <type_traits>

#include "db/access/heap_scan.hpp"

namespace {

template <typename Type>
concept ExposesImplementation = requires { typename Type::Impl; };

static_assert(std::is_nothrow_move_constructible_v<db::access::HeapScan>);
static_assert(std::is_nothrow_move_assignable_v<db::access::HeapScan>);
static_assert(!std::is_copy_constructible_v<db::access::HeapScan>);
static_assert(std::is_aggregate_v<db::access::HeapScanContext>);
static_assert(std::is_aggregate_v<db::access::HeapScanTuple>);
static_assert(!ExposesImplementation<db::access::HeapScan>);

}  // namespace
