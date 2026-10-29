#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "db/access/heap_delete.hpp"
#include "db/access/heap_fetch.hpp"
#include "db/access/heap_maintenance.hpp"
#include "db/access/heap_update.hpp"
#include "db/access/heap_wal.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/free_space_map.hpp"

namespace db::access {

inline constexpr std::uint8_t kDefaultHeapFillfactor = 80;
inline constexpr std::uint8_t kMinimumHeapFillfactor = 10;

enum class HeapSpacePolicy : std::uint8_t {
    kOrdinaryInsert,
    kHotUpdate,
};

class HeapRelationSpace;
class HeapScan;
class HeapIndexTidClassifier;

// Composition-root-owned registry. It keeps extension serialization, stable
// page count, and FSM state shared by every open handle for one relation.
class HeapSpaceRegistry final {
public:
    HeapSpaceRegistry();
    HeapSpaceRegistry(const HeapSpaceRegistry&) = delete;
    auto operator=(const HeapSpaceRegistry&) -> HeapSpaceRegistry& = delete;
    HeapSpaceRegistry(HeapSpaceRegistry&&) = delete;
    auto operator=(HeapSpaceRegistry&&) -> HeapSpaceRegistry& = delete;
    ~HeapSpaceRegistry();

    auto install(base::RelationId relation_id, storage::FreeSpaceMap free_space_map)
        -> base::Status;
    auto remove(base::RelationId relation_id) -> base::Status;
    auto contains(base::RelationId relation_id) const -> bool;
    auto size() const -> std::size_t;

private:
    struct Impl;

    friend class HeapTable;

    auto resolve(base::RelationId relation_id, std::uint64_t page_count)
        -> base::Result<std::shared_ptr<HeapRelationSpace>>;

    std::unique_ptr<Impl> implementation_;
};

// Move-only open relation descriptor. BufferPool and HeapSpaceRegistry outlive
// the table; relation space is shared and remains valid across handle reopen.
class HeapTable final {
public:
    static auto create(
        base::RelationId relation_id,
        storage::BufferPool& pool,
        HeapSpaceRegistry& space_registry,
        HeapMutationLogPort& mutation_log,
        std::uint8_t fillfactor = kDefaultHeapFillfactor
    ) -> base::Result<HeapTable>;
    static auto open(
        base::RelationId relation_id,
        storage::BufferPool& pool,
        HeapSpaceRegistry& space_registry,
        HeapMutationLogPort& mutation_log,
        std::uint8_t fillfactor = kDefaultHeapFillfactor
    ) -> base::Result<HeapTable>;

    HeapTable(const HeapTable&) = delete;
    auto operator=(const HeapTable&) -> HeapTable& = delete;
    HeapTable(HeapTable&& other) noexcept;
    auto operator=(HeapTable&& other) noexcept -> HeapTable&;
    ~HeapTable();

    // Moving transfers the open relation handle and leaves the source inert.
    auto valid() const noexcept -> bool;
    auto relation_id() const noexcept -> base::RelationId;
    auto fillfactor() const noexcept -> std::uint8_t;
    auto reserved_update_space() const noexcept -> std::uint16_t;
    auto page_count() const -> std::uint64_t;
    auto page_ids() const -> std::vector<base::PageId>;
    // Returns true only when every allocated heap page has no line pointers,
    // including dead, unused, and redirect slots. Callers that use this as a
    // bulk-build precondition must serialize relation writers for the complete
    // operation.
    auto is_physically_empty() const -> base::Result<bool>;

    auto ensure_initial_page() -> base::Result<base::PageId>;
    auto extend_page() -> base::Result<base::PageId>;
    auto insert(base::Xid xid, std::uint32_t command_id, base::Bytes payload)
        -> base::Result<base::Rid>;
    auto multi_insert(base::Xid xid, std::uint32_t command_id, std::span<const base::Bytes> rows)
        -> base::Result<std::vector<base::Rid>>;
    // RID and index entries are candidates. This operation pins the target
    // page once, follows only a valid same-page HOT chain, and returns the
    // first version visible to the supplied snapshot.
    auto fetch(base::Rid candidate, HeapFetchContext context) const
        -> base::Result<std::optional<HeapFetchTuple>>;
    auto update(HeapUpdateRequest request, HeapUpdateContext context)
        -> base::Result<HeapUpdateOutcome>;
    auto delete_tuple(HeapDeleteRequest request, HeapMutationContext context)
        -> base::Result<HeapDeleteOutcome>;
    auto vacuum(const transaction::CommitLog& commit_log, base::Xid oldest_xmin)
        -> base::Result<HeapVacuumStats>;
    // The composition layer calls this only after every index has consumed
    // the same dead-TID store. Busy heap pages remain dead and retryable.
    auto reclaim_index_cleaned_tids(const DeadTidStore& dead_tids)
        -> base::Result<HeapIndexCleanupStats>;
    auto find_page_with_space(std::uint16_t tuple_bytes, HeapSpacePolicy policy)
        -> base::Result<std::optional<base::PageId>>;
    auto refresh_free_space(base::PageId page_id) -> base::Status;
    auto record_free_space(base::PageId page_id, std::uint16_t free_bytes) const -> base::Status;
    auto persist_free_space_map() -> base::Status;
    auto sync_relation() -> base::Status;

private:
    struct Impl;
    class VacuumOperation;
    class IndexCleanupOperation;

    friend class HeapScan;
    friend class HeapIndexTidClassifier;

    explicit HeapTable(std::unique_ptr<Impl> implementation) noexcept;
    auto buffer_pool() const noexcept -> storage::BufferPool&;
    auto mutation_log() const noexcept -> HeapMutationLogPort&;
    auto try_prune_pinned(
        const storage::PinGuard& pin,
        const transaction::CommitLog& commit_log,
        base::Xid oldest_xmin
    ) const -> base::Result<bool>;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::access
