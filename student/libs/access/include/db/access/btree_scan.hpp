#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "db/access/btree_relation.hpp"
#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/transaction/ssi.hpp"
#include "db/transaction/state.hpp"
#include "db/types/value.hpp"

namespace db::access {

// The BtreeRelation descriptor and every referenced service must outlive the
// scan. The scan borrows the relation's immutable key layout instead of copying
// schema vectors per cursor. Supplying the optional SSI manager records one
// relation SIREAD for a rootless index and one page SIREAD for each leaf read
// by this scan.
struct BtreeScanContext final {
    base::Xid own_xid{base::kInvalidXid};
    transaction::IsolationLevel isolation{transaction::IsolationLevel::kReadCommitted};
    transaction::SerializableConflictManager* serializable_conflicts{nullptr};
};

// Inclusive/exclusive lexicographic bounds over leading key columns. Either
// bound may be empty (unbounded); otherwise its values must form a non-empty
// prefix of the relation key. A short inclusive upper bound includes every
// key with an equal prefix, while a short exclusive bound excludes that whole
// prefix group.
struct BtreeScanRange final {
    std::span<const types::Value> low;
    std::span<const types::Value> high;
    bool low_exclusive{false};
    bool high_exclusive{false};
};

// One RID copied from a leaf page while the scan held its read latch. The
// leaf identity is an opaque cleanup hint: cleanup matches the key/RID again
// under a write latch because a concurrent split may have moved the tuple
// after this candidate was cached.
struct BtreeScanCandidate final {
    base::Rid rid{base::kInvalidRid};
    base::RelationId relation_id{base::kInvalidRelationId};
    base::PageId leaf_page_id{base::kInvalidPageId};

    auto operator==(const BtreeScanCandidate&) const -> bool = default;
};

// Move-only B+Tree leaf scan. It lazily descends to a boundary leaf, copies
// matching RIDs from one read-latched leaf into a scan-local cache, then
// releases the latch before returning candidates. `next()` traverses right;
// `previous()` traverses left. A scan chooses one direction on its first read;
// callers create a new scan to traverse the opposite direction.
class BtreeScan final {
public:
    static auto for_eq(
        const BtreeRelation& tree,
        std::span<const types::Value> values,
        BtreeScanContext context
    ) -> base::Result<BtreeScan>;
    static auto for_range(const BtreeRelation& tree, BtreeScanRange range, BtreeScanContext context)
        -> base::Result<BtreeScan>;
    // Equality values bind a non-empty leading prefix. Range values bind the
    // columns immediately after that prefix; an empty lower or upper range
    // side inherits the inclusive equality-prefix boundary.
    static auto for_eq_range(
        const BtreeRelation& tree,
        std::span<const types::Value> equality_prefix,
        BtreeScanRange range,
        BtreeScanContext context
    ) -> base::Result<BtreeScan>;
    static auto for_full(const BtreeRelation& tree, BtreeScanContext context)
        -> base::Result<BtreeScan>;

    BtreeScan(const BtreeScan&) = delete;
    auto operator=(const BtreeScan&) -> BtreeScan& = delete;
    BtreeScan(BtreeScan&& other) noexcept;
    auto operator=(BtreeScan&& other) noexcept -> BtreeScan&;
    ~BtreeScan();

    // Moving transfers the cursor and leaves the source as an inert value.
    auto valid() const noexcept -> bool;
    auto next_candidate(storage::BufferPool& pool)
        -> base::Result<std::optional<BtreeScanCandidate>>;
    auto next(storage::BufferPool& pool) -> base::Result<std::optional<base::Rid>>;
    auto previous_candidate(storage::BufferPool& pool)
        -> base::Result<std::optional<BtreeScanCandidate>>;
    auto previous(storage::BufferPool& pool) -> base::Result<std::optional<base::Rid>>;

    // Prepared/executor adapters re-evaluate parameter values through these
    // operations. Successful rebinds drop every positional/RID cache while
    // retaining the relation and PG-style fast-root hint; bound construction
    // failure leaves both the active bounds and current cursor unchanged. rescan()
    // repeats the current logical bounds and may choose either direction.
    auto rebind_eq(std::span<const types::Value> values) -> base::Status;
    auto rebind_range(BtreeScanRange range) -> base::Status;
    auto rebind_eq_range(std::span<const types::Value> equality_prefix, BtreeScanRange range)
        -> base::Status;
    void rescan() noexcept;

    // Observability: real leaf pages read and leaf tuples examined after
    // positioning. Kept unconditional so tests observe actual scan work.
    auto leaf_pages_visited() const noexcept -> std::size_t;
    auto leaf_items_examined() const noexcept -> std::size_t;
    auto root_hint_uses() const noexcept -> std::size_t;

private:
    struct Impl;

    explicit BtreeScan(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::access
