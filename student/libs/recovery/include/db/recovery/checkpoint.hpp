#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::catalog {
class CatalogStore;
}  // namespace db::catalog

namespace db::storage {
class BufferPool;
}  // namespace db::storage

namespace db::transaction {
class TransactionManager;
}  // namespace db::transaction

namespace db::wal {
class WalControlStore;
class WalManager;
}  // namespace db::wal

namespace db::recovery {

// FCK1 is a recovery-owned, versioned main payload for the final System
// checkpoint record. It is intentionally distinct from the enclosing WAL v1
// record format so future recovery dispatch can reject a mismatched payload
// independently.
inline constexpr std::uint32_t kCheckpointPayloadMagic = 0x314B'4346U;  // "FCK1" little endian.
inline constexpr std::uint16_t kCheckpointPayloadVersion = 1U;
inline constexpr std::size_t kCheckpointPayloadSize = 20U;

struct CheckpointPayload final {
    base::Xid next_xid{base::Xid{1}};
    base::Xid latest_completed_xid{base::kInvalidXid};
    std::uint32_t next_object_id{1U};

    auto operator==(const CheckpointPayload&) const noexcept -> bool = default;
};

auto encode_checkpoint_payload(const CheckpointPayload& payload)
    -> base::Result<std::vector<std::byte>>;
auto decode_checkpoint_payload(base::Bytes bytes) -> base::Result<CheckpointPayload>;

inline constexpr std::size_t kDefaultCheckpointDirtyPageBatchFrames = 32U;

// All references are borrowed for one run. The caller owns every service and
// keeps it alive until CheckpointCoordinator::run returns.
struct CheckpointContext final {
    catalog::CatalogStore& catalog;
    transaction::TransactionManager& transaction_manager;
    storage::BufferPool& buffer_pool;
    wal::WalControlStore& control_store;
    wal::WalManager& wal_manager;
};

struct CheckpointOptions final {
    std::size_t dirty_page_batch_frames{kDefaultCheckpointDirtyPageBatchFrames};
    // The caller must keep a database admission barrier closed for the whole
    // run. Resolve abandoned owners and reject any remaining authoritative
    // ProcArray activity before inserting the redo marker.
    bool require_quiescence{false};
};

struct CheckpointResult final {
    base::Lsn redo_lsn{base::kInvalidLsn};
    base::Lsn checkpoint_lsn{base::kInvalidLsn};
    base::Lsn checkpoint_end_lsn{base::kInvalidLsn};
    std::size_t marked_dirty_pages{0U};
    std::size_t flushed_dirty_pages{0U};

    auto operator==(const CheckpointResult&) const noexcept -> bool = default;
};

// Serializes one complete fuzzy-checkpoint generation. Its mutex covers the
// redo marker, dirty snapshot, final record, control publication, and WAL
// recycling, preventing a newer generation from invalidating an older run's
// redo point. Transaction completion is blocked only while the marker is
// inserted; ordinary mutations can continue while dirty pages are drained.
// One composition root must share one coordinator among all callers that
// checkpoint the same live service set.
class CheckpointCoordinator final {
public:
    CheckpointCoordinator() = default;
    ~CheckpointCoordinator() = default;
    CheckpointCoordinator(const CheckpointCoordinator&) = delete;
    auto operator=(const CheckpointCoordinator&) -> CheckpointCoordinator& = delete;
    CheckpointCoordinator(CheckpointCoordinator&&) = delete;
    auto operator=(CheckpointCoordinator&&) -> CheckpointCoordinator& = delete;

    auto run(CheckpointContext context, CheckpointOptions options = {})
        -> base::Result<CheckpointResult>;

private:
    std::mutex mutex_;
};

}  // namespace db::recovery
