#pragma once

#include <cstddef>
#include <memory>
#include <span>

#include "db/access/btree_scan.hpp"
#include "db/base/result.hpp"
#include "db/types/value.hpp"

namespace db::access {

struct BtreeCleanupStats final {
    std::size_t pages_changed{0U};
    std::size_t pages_busy{0U};
    std::size_t rids_removed{0U};
};

// Scan-driven, nonblocking simple deletion for one complete equality key.
// Candidate page IDs are hints; cleanup matches each key/RID again while the
// leaf owns an exclusive content latch.
class BtreeCleanupBatch final {
public:
    static auto for_equal(const BtreeRelation& relation, std::span<const types::Value> values)
        -> base::Result<BtreeCleanupBatch>;

    BtreeCleanupBatch(const BtreeCleanupBatch&) = delete;
    auto operator=(const BtreeCleanupBatch&) -> BtreeCleanupBatch& = delete;
    BtreeCleanupBatch(BtreeCleanupBatch&& other) noexcept;
    auto operator=(BtreeCleanupBatch&& other) noexcept -> BtreeCleanupBatch&;
    ~BtreeCleanupBatch();

    auto valid() const noexcept -> bool;
    auto reset_equal(const BtreeRelation& relation, std::span<const types::Value> values)
        -> base::Status;
    auto push_dead(BtreeScanCandidate candidate) -> base::Status;
    auto empty() const noexcept -> bool;
    auto flush(storage::BufferPool& pool) -> base::Result<BtreeCleanupStats>;

private:
    struct Impl;

    explicit BtreeCleanupBatch(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::access
