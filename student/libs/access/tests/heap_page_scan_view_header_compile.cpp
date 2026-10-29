#include "heap_page_scan_view.hpp"

namespace db::access {

static_assert(sizeof(HeapPageScanView) == sizeof(std::span<const std::byte, storage::kPageSize>));

}  // namespace db::access
