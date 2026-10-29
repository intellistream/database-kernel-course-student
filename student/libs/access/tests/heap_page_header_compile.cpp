#include "db/access/heap_page.hpp"

namespace {

template <typename Type>
concept ExposesCompactionOperation = requires { typename Type::CompactionOperation; };

template <typename Type>
concept ExposesPruneOperation = requires { typename Type::PruneOperation; };

static_assert(db::access::kTupleHeaderSize == 24);
static_assert(db::access::kMaximumHeapRowSize == 4'036);
static_assert(db::access::kLinePointerSize == 4);
static_assert(!ExposesCompactionOperation<db::access::HeapPage>);
static_assert(!ExposesPruneOperation<db::access::HeapPage>);

}  // namespace
