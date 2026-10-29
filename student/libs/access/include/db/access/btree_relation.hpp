#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "db/access/btree_key.hpp"
#include "db/access/btree_page.hpp"
#include "db/access/btree_wal.hpp"
#include "db/access/index_tid.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/buffer_pool.hpp"

namespace db::access {

class BtreeScan;
class DeadTidStore;

// Heap-derived input for one bottom-up index build. Callers obtain validated,
// layout-bound entries through BtreeRelation rather than constructing encoded
// keys from an unrelated relation contract.
struct BtreeBulkBuildEntry final {
    EncodedBtreeKey key;
    base::Rid heap_rid{base::kInvalidRid};
};

// Relation-bound streaming input for large bottom-up builds. Encoded keys
// share one contiguous arena; slots keep offsets so arena growth never
// invalidates them. A producer may append one batch of keys before heap RIDs
// are known, then attach that batch's RIDs in one call.
class BtreeBulkBuildBuffer final {
public:
    BtreeBulkBuildBuffer(const BtreeBulkBuildBuffer&) = delete;
    auto operator=(const BtreeBulkBuildBuffer&) -> BtreeBulkBuildBuffer& = delete;
    BtreeBulkBuildBuffer(BtreeBulkBuildBuffer&&) noexcept = default;
    auto operator=(BtreeBulkBuildBuffer&&) noexcept -> BtreeBulkBuildBuffer& = default;
    ~BtreeBulkBuildBuffer() = default;

    auto size() const noexcept -> std::size_t;
    auto empty() const noexcept -> bool;
    auto key(std::size_t index) const noexcept -> base::Bytes;
    auto heap_rid(std::size_t index) const noexcept -> base::Rid;

private:
    struct Slot final {
        std::size_t key_offset{0U};
        std::size_t key_size{0U};
        base::Rid heap_rid{base::kInvalidRid};
    };

    BtreeBulkBuildBuffer(base::RelationId relation_id, std::uint64_t type_signature) noexcept;

    auto append_encoded(base::Bytes key, base::Rid heap_rid) -> base::Status;
    auto attach_rids(std::span<const base::Rid> heap_rids) -> base::Status;
    void sort(const BtreeKeyLayout& key_layout);

    friend class BtreeRelation;

    base::RelationId relation_id_{base::kInvalidRelationId};
    std::uint64_t type_signature_{0U};
    std::vector<std::byte> key_bytes_;
    std::vector<Slot> slots_;
    std::size_t attached_slots_{0U};
    bool keys_are_canonical_{true};
    EncodedBtreeKey scratch_key_;
};

struct BtreeBulkBuildStats final {
    std::size_t entries{0U};
    std::size_t leaf_pages{0U};
    std::size_t internal_pages{0U};
    std::optional<BtreeRoot> root;
};

struct BtreeVacuumStats final {
    std::size_t pages_scanned{0U};
    std::size_t pages_compacted{0U};
    std::size_t rids_removed{0U};
};

// Move-only open B+Tree relation descriptor. The BufferPool outlives the
// relation; the immutable key layout is copied per handle. A relation-local
// root hint behaves like PostgreSQL's fast root: after a root split, the old
// root remains a valid leftmost B-link descent origin at its original level.
class BtreeRelation final {
public:
    static auto create(
        base::RelationId relation_id,
        storage::BufferPool& pool,
        BtreeKeyLayout key_layout,
        bool is_unique,
        BtreeMutationLogPort& mutation_log
    ) -> base::Result<BtreeRelation>;
    static auto open(
        base::RelationId relation_id,
        storage::BufferPool& pool,
        BtreeKeyLayout key_layout,
        bool is_unique,
        BtreeMutationLogPort& mutation_log
    ) -> base::Result<BtreeRelation>;

    BtreeRelation(const BtreeRelation&) = delete;
    auto operator=(const BtreeRelation&) -> BtreeRelation& = delete;
    BtreeRelation(BtreeRelation&& other) noexcept;
    auto operator=(BtreeRelation&& other) noexcept -> BtreeRelation&;
    ~BtreeRelation() = default;

    auto valid() const noexcept -> bool;
    auto relation_id() const noexcept -> base::RelationId;
    auto key_layout() const noexcept -> const BtreeKeyLayout&;
    auto is_unique() const noexcept -> bool;

    // Allocates and publishes the first, empty root leaf. The relation must
    // be rootless. Concurrent first-root creators serialize on the metapage;
    // no pre-existing data-root latch can participate in that transition.
    auto create_empty_root(storage::BufferPool& pool) const -> base::Result<BtreeRoot>;

    // Reads the current root from the metapage. A newly created relation is
    // rootless and therefore returns an empty optional rather than a sentinel
    // that could be mistaken for a valid page.
    auto root(storage::BufferPool& pool) const -> base::Result<std::optional<BtreeRoot>>;

    auto make_bulk_build_entry(std::span<const types::Value> values, base::Rid heap_rid) const
        -> base::Result<BtreeBulkBuildEntry>;
    auto make_projected_bulk_build_entry(
        std::span<const types::Value> source_values,
        std::span<const std::size_t> positions,
        base::Rid heap_rid
    ) const -> base::Result<BtreeBulkBuildEntry>;

    auto make_bulk_build_buffer() const -> base::Result<BtreeBulkBuildBuffer>;
    auto append_projected_canonical_bulk_build_key(
        BtreeBulkBuildBuffer& buffer,
        std::span<const types::Value> source_values,
        std::span<const std::size_t> positions
    ) const -> base::Status;
    auto
    attach_bulk_build_rids(BtreeBulkBuildBuffer& buffer, std::span<const base::Rid> heap_rids) const
        -> base::Status;

    // Sorts heap-derived entries by (typed key, RID), checks a unique index
    // before allocating pages, builds every level left-to-right, and publishes
    // the finished root last. The relation must still be rootless.
    auto
    bulk_build(storage::BufferPool& pool, const std::vector<BtreeBulkBuildEntry>& entries) const
        -> base::Result<BtreeBulkBuildStats>;
    auto bulk_build(storage::BufferPool& pool, BtreeBulkBuildBuffer entries) const
        -> base::Result<BtreeBulkBuildStats>;

    // Removes every entry whose heap RID is present in the stable dead-TID
    // set and atomically rebuilds changed leaves. Pages remain linked and
    // allocated; safe page deletion requires a later half-dead protocol.
    auto bulk_delete(storage::BufferPool& pool, const DeadTidStore& dead_tids) const
        -> base::Result<BtreeVacuumStats>;
    auto vacuum(storage::BufferPool& pool) const -> base::Result<BtreeVacuumStats>;

    // Inserts one complete composite key and heap RID. Writers descend with
    // coupled read latches, write-latch only the target leaf, and propagate a
    // split through short parent write-latch acquisitions. A unique relation
    // applies structural duplicate detection; SQL DML supplies the heap-aware
    // overload below.
    auto insert(
        storage::BufferPool& pool,
        std::span<const types::Value> values,
        base::Rid heap_rid
    ) const -> base::Status;

    // Heap-aware unique insertion. Live entries conflict, retained versions
    // remain, dead entries on the insertion leaf are retired, and an
    // in-progress conflict is waited for only after every B-tree guard drops.
    auto insert(
        storage::BufferPool& pool,
        std::span<const types::Value> values,
        base::Rid heap_rid,
        BtreeInsertContext context
    ) const -> base::Status;

private:
    class State;

    friend class BtreeCleanupBatch;
    friend class BtreeScan;

    BtreeRelation(
        base::RelationId relation_id,
        BtreeKeyLayout key_layout,
        bool is_unique,
        BtreeMutationLogPort& mutation_log,
        std::shared_ptr<State> state
    ) noexcept;

    auto relation_state() const noexcept -> const std::shared_ptr<State>&;
    auto mutation_log() const noexcept -> BtreeMutationLogPort&;
    auto insert_impl(
        storage::BufferPool& pool,
        std::span<const types::Value> values,
        base::Rid heap_rid,
        const BtreeInsertContext* context
    ) const -> base::Status;

    base::RelationId relation_id_;
    BtreeKeyLayout key_layout_;
    bool is_unique_;
    BtreeMutationLogPort* mutation_log_;
    std::shared_ptr<State> relation_state_;
};

}  // namespace db::access
