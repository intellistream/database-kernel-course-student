#pragma once

#include <cstddef>

#include "db/access/dead_tid_store.hpp"

namespace db::access {

struct HeapVacuumStats final {
    std::size_t pages_scanned{0};
    std::size_t pages_skipped_busy{0};
    std::size_t tuples_pruned{0};
    std::size_t redirects_installed{0};
    std::size_t space_reclaimed{0};
    std::size_t line_pointers_truncated{0};
    DeadTidStore dead_tids;
};

struct HeapIndexCleanupStats final {
    std::size_t pages_scanned{0};
    std::size_t pages_skipped_busy{0};
    std::size_t tids_reclaimed{0};
    std::size_t space_reclaimed{0};
    std::size_t line_pointers_truncated{0};
};

}  // namespace db::access
