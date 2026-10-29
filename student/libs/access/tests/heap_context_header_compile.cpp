#include <type_traits>

#include "db/access/heap_context.hpp"

namespace {

static_assert(std::is_aggregate_v<db::access::HeapMvccContext>);
static_assert(std::is_trivially_copyable_v<db::access::HeapMvccContext>);

}  // namespace
