#include "db/access/heap_wal.hpp"

namespace {

static_assert(db::access::kMaximumHeapMultiInsertRows > 0U);
static_assert(db::access::kMaximumHeapMultiInsertPayloadBytes > 0U);

}  // namespace
