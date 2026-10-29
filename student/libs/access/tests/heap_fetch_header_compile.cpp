#include <type_traits>

#include "db/access/heap_fetch.hpp"

static_assert(std::is_aggregate_v<db::access::HeapFetchContext>);
static_assert(std::is_aggregate_v<db::access::HeapFetchTuple>);
