#pragma once

#include <cstddef>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/recovery/btree_startup_redo.hpp"
#include "db/recovery/durable_state_redo.hpp"
#include "db/recovery/recovery_page_cache.hpp"

namespace db::recovery {

class RecoveryWalStream;

// Borrowed composition inputs for one complete, single-threaded crash-replay
// pass. The caller owns all services and keeps them alive through the return
// from run_startup_recovery(); this runner does not construct a normal buffer
// pool, transaction manager, catalog store, or WAL writer.
struct StartupRecoveryRunContext final {
    RecoveryWalStream& wal_stream;
    RecoveryPageCache& page_cache;
    DurableStateReplayer& durable_state;
};

// Immutable summary of a fully drained recovery pass. Page counters are
// record-local redo dispositions; cache statistics are captured only after the
// recovery cache has written and synced every remaining page.
struct StartupRecoveryReport final {
    std::size_t records_replayed{0U};
    std::size_t checkpoint_records_replayed{0U};
    std::size_t checkpoint_redo_markers_replayed{0U};
    std::size_t transaction_records_replayed{0U};
    std::size_t heap_records_replayed{0U};
    std::size_t heap_pages_applied{0U};
    std::size_t heap_pages_skipped{0U};
    std::size_t btree_records_replayed{0U};
    std::size_t btree_pages_applied{0U};
    std::size_t btree_pages_skipped{0U};
    base::Lsn replay_start_lsn{base::kInvalidLsn};
    base::Lsn last_lsn{base::kInvalidLsn};
    bool repaired_incomplete_tail{false};
    DurableStateReplayResult durable_state;
    RecoveryPageCacheStats page_cache;

    auto operator==(const StartupRecoveryReport&) const -> bool = default;
};

// Replays every record in one validated RecoveryWalStream, drains/syncs the
// recovery cache, then finalizes CLOG and the catalog OID frontier. A failed
// pass explicitly abandons only in-memory recovery candidates while control
// remains outside production; the next startup retries from the same durable
// WAL. Physical Catalog WAL records are deterministically rejected until the
// project defines a versioned producer and replay payload for that route.
auto run_startup_recovery(StartupRecoveryRunContext context) -> base::Result<StartupRecoveryReport>;

}  // namespace db::recovery
