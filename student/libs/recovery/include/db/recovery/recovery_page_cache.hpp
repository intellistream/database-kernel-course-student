#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/recovery/startup_page_access.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/durability.hpp"

namespace db::recovery {

// Bounded, single-threaded startup cache configuration. The cache is separate
// from the normal BufferPool: recovery owns its residency and drain lifetime,
// while the composition root keeps the borrowed disk and durability services
// alive for the cache's complete lifetime.
struct RecoveryPageCacheOptions final {
    std::size_t capacity{0U};
};

struct RecoveryPageCacheStats final {
    std::uint64_t page_reads{0U};
    std::uint64_t page_writes{0U};
    std::uint64_t cache_hits{0U};
    std::uint64_t cache_misses{0U};
    std::uint64_t evictions{0U};
    std::uint64_t replacement_probes{0U};
    std::uint64_t allocated_pages{0U};
    std::uint64_t relation_syncs{0U};

    auto operator==(const RecoveryPageCacheStats&) const noexcept -> bool = default;
};

// Implements the page-access port consumed by Heap and B+Tree startup redo.
// A cache entry remains pinned for the lifetime of its returned lease, and
// cache replacement never evicts such an entry. `drain_and_sync()` is an
// explicit error-returning durability boundary; callers must release every
// lease before invoking it. It is retry-safe after a write or sync failure and
// seals the cache against later acquisitions after success. Destroying a cache
// with a live lease or unflushed recovery mutation terminates: recovery must
// handle an explicit drain error rather than silently lose durable state.
class RecoveryPageCache final : public StartupPageAccessPort {
public:
    static auto create(
        storage::DiskManager& disk,
        storage::PageDurabilityPort& durability,
        RecoveryPageCacheOptions options
    ) -> base::Result<std::unique_ptr<RecoveryPageCache>>;

    RecoveryPageCache(const RecoveryPageCache&) = delete;
    auto operator=(const RecoveryPageCache&) -> RecoveryPageCache& = delete;
    RecoveryPageCache(RecoveryPageCache&&) = delete;
    auto operator=(RecoveryPageCache&&) -> RecoveryPageCache& = delete;
    ~RecoveryPageCache() override;

    auto acquire(StartupPageRequest request)
        -> base::Result<std::unique_ptr<StartupPageLease>> override;

    // Flushes every dirty cached page in (relation, page) order, then syncs
    // each written or newly allocated relation in relation-ID order. Page
    // writeback invokes PageDurabilityPort before data I/O. A failure leaves
    // remaining dirty pages or relations pending so this call may be retried.
    auto drain_and_sync() -> base::Status;

    // Explicitly discards only in-memory recovery candidates after a failed
    // startup attempt. It never writes or syncs a page. Callers may use it
    // only while the durable control state remains recoverable, so the next
    // startup replays the still-authoritative WAL. Active leases and an
    // in-progress drain are rejected; a successful drain remains final.
    auto abandon_for_retry() -> base::Status;

    auto stats() const noexcept -> RecoveryPageCacheStats;
    auto cached_page_count() const noexcept -> std::size_t;
    auto contains(storage::PageAddress address) const noexcept -> bool;
    auto is_finalized() const noexcept -> bool;
    auto is_abandoned() const noexcept -> bool;

private:
    struct Impl;

    explicit RecoveryPageCache(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::recovery
