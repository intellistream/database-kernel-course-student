#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/access/btree_wal.hpp"
#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/storage/page.hpp"
#include "db/wal/delta.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"

namespace db::recovery {

inline constexpr std::uint32_t kBtreeWalPayloadMagic = 0x3150'5742U;  // "BWP1" little endian.
inline constexpr std::uint16_t kBtreeWalPayloadVersion = 1U;
inline constexpr std::size_t kBtreeWalPayloadHeaderSize = 12U;

struct BtreeWalPayload final {
    access::BtreeWalOperation operation{access::BtreeWalOperation::kInsertLeaf};
    access::BtreeWalPageRole role{access::BtreeWalPageRole::kPrimary};
    wal::PageDelta patches;

    auto operator==(const BtreeWalPayload&) const -> bool = default;
};

auto encode_btree_wal_payload(const BtreeWalPayload& payload)
    -> base::Result<std::vector<std::byte>>;
auto decode_btree_wal_payload(std::uint8_t info, base::Bytes bytes)
    -> base::Result<BtreeWalPayload>;

class BtreeMutationWalAdapter final : public access::BtreeMutationLogPort {
public:
    explicit BtreeMutationWalAdapter(wal::WalManager& manager) noexcept;

    auto log(const access::BtreeWalMutation& mutation) -> base::Result<base::Lsn> override;

private:
    wal::WalManager* manager_;
};

enum class BtreeRedoDisposition : std::uint8_t {
    kApplied,
    kSkipped,
};

// Applies one selected block from a fully validated B+Tree WAL record. The
// caller owns page caching; resident BTree page contents are trusted.
auto apply_btree_redo(
    const wal::DecodedWalRecord& decoded,
    std::size_t block_index,
    storage::Page& page
) -> base::Result<BtreeRedoDisposition>;

}  // namespace db::recovery
