#include "db/access/heap_maintenance.hpp"

namespace db::access {

[[maybe_unused]] void heap_maintenance_header_compile() {
    const HeapVacuumStats stats{};
    static_cast<void>(stats.dead_tids.empty());
}

}  // namespace db::access
