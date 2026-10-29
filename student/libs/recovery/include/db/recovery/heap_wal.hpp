#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/access/heap_page.hpp"
#include "db/access/heap_wal.hpp"
#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/page.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"

namespace db::recovery {

inline constexpr std::uint32_t kHeapWalPayloadMagic = 0x3150'5748U;  // "HWP1" little endian.
inline constexpr std::uint16_t kHeapWalPayloadVersion = 1;
inline constexpr std::size_t kHeapWalPayloadHeaderSize = 8;

struct HeapWalTupleData final {
    base::SlotId slot{base::kInvalidSlotId};
    std::vector<std::byte> bytes;

    auto operator==(const HeapWalTupleData&) const -> bool = default;
};

// Closed semantic payload used by the heap resource manager. Operation-specific
// validation fixes which fields are present and their canonical ordering.
struct HeapWalPayload final {
    access::HeapWalOperation operation{access::HeapWalOperation::kInitialize};
    std::uint16_t page_flags{0};
    std::vector<HeapWalTupleData> tuples;
    std::vector<access::HeapPruneRedirect> redirects;
    std::vector<base::SlotId> dead;
    std::vector<base::SlotId> unused;

    auto operator==(const HeapWalPayload&) const -> bool = default;
};

auto encode_heap_wal_payload(const HeapWalPayload& payload) -> base::Result<std::vector<std::byte>>;
auto decode_heap_wal_payload(std::uint8_t info, base::Bytes bytes) -> base::Result<HeapWalPayload>;

// Composition-layer adapter from access's borrowed mutation port to concrete
// WAL insertion. WalManager and every borrowed page outlive the call.
class HeapMutationWalAdapter final : public access::HeapMutationLogPort {
public:
    explicit HeapMutationWalAdapter(wal::WalManager& manager) noexcept;

    auto log(const access::HeapWalMutation& mutation) -> base::Result<base::Lsn> override;

private:
    wal::WalManager* manager_;
};

enum class HeapRedoDisposition : std::uint8_t {
    kApplied,
    kSkipped,
};

// Page-local idempotent redo hook. Startup orchestration and the recovery page
// cache remain separate composition responsibilities.
auto apply_heap_redo(const wal::DecodedWalRecord& decoded, storage::Page& page)
    -> base::Result<HeapRedoDisposition>;

}  // namespace db::recovery
