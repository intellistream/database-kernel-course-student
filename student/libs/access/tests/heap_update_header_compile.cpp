#include "db/access/heap_update.hpp"

namespace {

[[maybe_unused]] void heap_update_header_compile() {
    const db::access::HeapUpdateSuccess result{};
    static_cast<void>(result.index_maintenance_required());
}

}  // namespace
