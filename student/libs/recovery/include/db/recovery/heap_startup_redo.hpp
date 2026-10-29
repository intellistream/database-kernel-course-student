#pragma once

#include "db/base/result.hpp"
#include "db/recovery/heap_wal.hpp"
#include "db/recovery/startup_page_access.hpp"

namespace db::recovery {

// Replays one Heap-routed decoded WAL record through caller-owned startup page
// access. Semantic validation completes before acquire(), FPI/WILL_INIT avoid
// reading an untrusted old page, and a lease is marked dirty only after the
// page-local redo adapter has atomically published its candidate.
auto replay_heap_startup_redo(const wal::DecodedWalRecord& decoded, StartupPageAccessPort& pages)
    -> base::Result<HeapRedoDisposition>;

}  // namespace db::recovery
