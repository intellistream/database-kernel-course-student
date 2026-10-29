#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/durability.hpp"
#include "db/storage/frame.hpp"
#include "db/storage/page.hpp"
#include "db/storage/replacement.hpp"

namespace db::storage {

struct CheckpointDirtySnapshot final {
    std::size_t marked{0};
    base::Lsn max_lsn{base::kInvalidLsn};

    auto operator==(const CheckpointDirtySnapshot&) const noexcept -> bool = default;
};

class BufferPool final {
public:
    static auto create(
        std::size_t pool_size,
        std::unique_ptr<DiskManager> disk,
        std::unique_ptr<ReplacementPolicy> replacement = nullptr,
        std::unique_ptr<PageDurabilityPort> durability = nullptr
    ) -> base::Result<std::unique_ptr<BufferPool>>;

    BufferPool(const BufferPool&) = delete;
    auto operator=(const BufferPool&) -> BufferPool& = delete;
    BufferPool(BufferPool&&) = delete;
    auto operator=(BufferPool&&) -> BufferPool& = delete;
    ~BufferPool();

    auto pool_size() const noexcept -> std::size_t;
    auto is_shutdown() const noexcept -> bool;

    auto pin_page(PageAddress address) -> base::Result<PinGuard>;
    auto read_page(PageAddress address) -> base::Result<PageReadGuard>;
    auto write_page(PageAddress address) -> base::Result<PageWriteGuard>;
    auto try_write_page(PageAddress address) -> base::Result<PageWriteGuard>;
    auto allocate_page(base::RelationId relation_id, PageKind kind, std::size_t special_size = 0)
        -> base::Result<PinGuard>;
    auto relation_page_count(base::RelationId relation_id) -> base::Result<std::uint64_t>;
    auto sync_relation(base::RelationId relation_id) -> base::Status;
    // Retires all cached pages for one relation and removes its durable file.
    // The caller serializes DDL and excludes SQL users of that relation for
    // this call. Pinned or in-flight pages are rejected rather than discarded.
    auto remove_relation(base::RelationId relation_id, bool missing_ok = false) -> base::Status;

    auto flush_page(PageAddress address) -> base::Result<bool>;
    auto flush_all() -> base::Status;
    auto sync_all() -> base::Status;

    // Captures the opening dirty set for one fuzzy checkpoint. The batch API
    // scans at most frame_budget frame slots and advances cursor circularly.
    auto mark_checkpoint_dirty_pages() -> base::Result<CheckpointDirtySnapshot>;
    auto flush_checkpoint_dirty_batch(
        std::size_t& cursor,
        std::size_t frame_budget,
        base::Lsn snapshot_max_lsn
    ) -> base::Result<std::size_t>;

    // The composition owner must stop concurrent operations and release every
    // page guard before shutdown. Repeated calls after success are harmless.
    auto shutdown() -> base::Status;

    auto contains(PageAddress address) const -> bool;
    auto frame_snapshot(PageAddress address) const -> base::Result<FrameSnapshot>;

private:
    struct Impl;

    explicit BufferPool(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::storage
