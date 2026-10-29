#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/page.hpp"

namespace db::access {

inline constexpr std::size_t kMaximumHeapMultiInsertRows = 1024;
inline constexpr std::size_t kMaximumHeapMultiInsertPayloadBytes = std::size_t{2} * 1024U * 1024U;

enum class HeapWalOperation : std::uint8_t {
    kInitialize,
    kInsert,
    kMultiInsert,
    kLock,
    kUpdate,
    kHotUpdate,
    kDelete,
    kPrune,
};

struct HeapWalTuple final {
    base::SlotId slot_id{base::kInvalidSlotId};
    base::Bytes bytes{};
};

// All pointers and spans are borrowed only for the duration of log(). The
// caller retains the page pin and exclusive content latch for that call.
struct HeapWalMutation final {
    HeapWalOperation operation{HeapWalOperation::kInitialize};
    base::Xid xid{base::kInvalidXid};
    storage::PageAddress address{base::kInvalidRelationId, base::kInvalidPageId};
    bool will_initialize{false};
    const storage::Page* before_page{nullptr};
    const storage::Page* after_page{nullptr};
    std::span<const HeapWalTuple> tuples{};
};

// Consumer-owned port implemented at a composition layer. A successful call
// has copied all borrowed data and returns the record end LSN. It must not
// re-enter the heap, mutate either borrowed page, or wait for data-page I/O
// while the page latch is held. The access method installs only the returned
// LSN in the generic page header; heap layout fields remain unchanged.
class HeapMutationLogPort {
public:
    HeapMutationLogPort() = default;
    HeapMutationLogPort(const HeapMutationLogPort&) = delete;
    auto operator=(const HeapMutationLogPort&) -> HeapMutationLogPort& = delete;
    HeapMutationLogPort(HeapMutationLogPort&&) = delete;
    auto operator=(HeapMutationLogPort&&) -> HeapMutationLogPort& = delete;
    virtual ~HeapMutationLogPort() = default;

    virtual auto log(const HeapWalMutation& mutation) -> base::Result<base::Lsn> = 0;

    // Direct-load composition may durably materialize a closed, empty-table
    // image without per-mutation WAL. Access methods then leave page LSNs
    // unchanged; the caller must establish a recovery boundary before making
    // the image committed.
    virtual auto writes_wal() const noexcept -> bool {
        return true;
    }
};

}  // namespace db::access
