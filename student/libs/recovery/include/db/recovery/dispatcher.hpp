#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/recovery/btree_wal.hpp"
#include "db/recovery/heap_wal.hpp"
#include "db/storage/page.hpp"

namespace db::wal {
struct DecodedWalRecord;
}  // namespace db::wal

namespace db::recovery {

using RecoveryBlockPayload = std::variant<std::monostate, HeapWalPayload, BtreeWalPayload>;

// The recovery-owned route is intentionally narrower than WalResourceManager:
// it records the exact opcode family the replay executor must handle.
enum class RecoveryRecordRoute : std::uint8_t {
    kCheckpoint,
    kCheckpointRedo,
    kTransactionCommit,
    kTransactionAbort,
    kHeap,
    kBtree,
    kCatalog,
};

// Delta replay is decided from a validated record plan and the target page's
// pre-replay LSN. Full images and WILL_INIT blocks deliberately bypass the
// ordinary LSN skip rule; page replay owns their physical installation order.
enum class RecoveryDeltaDisposition : std::uint8_t {
    kNoDelta,
    kApply,
    kSkipByPageLsn,
};

// Owned state for one decoded WAL block. It does not borrow the source WAL
// record. Heap and B+Tree deltas carry their decoded semantic payload directly,
// so redo does not parse the same bytes again after acquiring a page.
struct RecoveryBlockDispatch final {
    std::size_t block_index{0U};
    storage::PageAddress address{base::kInvalidRelationId, base::kInvalidPageId};
    bool will_initialize{false};
    bool standard_page{false};
    bool has_full_page_image{false};
    bool has_delta{false};
    RecoveryBlockPayload payload;

    auto operator==(const RecoveryBlockDispatch&) const -> bool = default;
};

// `dispatch()` only returns this type after checking its LSN envelope, rmgr,
// opcode, payload, and resource-manager block-role contract. The record's
// bytes remain owned by the caller; the dispatch owns routing metadata and any
// decoded page payload needed by redo. Every B+Tree block carries its validated
// operation/role payload, including FPI-only blocks whose patch set is empty.
struct RecoveryRecordDispatch final {
    RecoveryRecordRoute route{RecoveryRecordRoute::kCheckpoint};
    base::Lsn start_lsn{base::kInvalidLsn};
    base::Lsn end_lsn{base::kInvalidLsn};
    std::vector<RecoveryBlockDispatch> blocks;

    auto operator==(const RecoveryRecordDispatch&) const -> bool = default;
};

// Stateless semantic front end for recovery. WalReader owns physical framing,
// checksums, and generic block presence; this layer owns recovery-specific
// opcode, payload, block-role, and idempotence rules. It does not read, cache,
// or mutate relation pages.
class RecoveryDispatcher final {
public:
    auto dispatch(const wal::DecodedWalRecord& decoded) const
        -> base::Result<RecoveryRecordDispatch>;

    // Returns the delta action for one block in a dispatch plan. A full image
    // is always eligible for restoration, while a following delta must apply
    // after that restoration; WILL_INIT similarly replaces its old base.
    static auto delta_disposition(
        const RecoveryRecordDispatch& dispatch,
        std::size_t block_index,
        base::Lsn page_lsn
    ) -> base::Result<RecoveryDeltaDisposition>;
};

}  // namespace db::recovery
