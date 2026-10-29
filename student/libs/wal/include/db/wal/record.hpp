#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/wal/delta.hpp"
#include "db/wal/format.hpp"
#include "db/wal/resource_manager.hpp"

namespace db::wal {

namespace opcode {

namespace system {
inline constexpr std::uint8_t kCheckpoint = 0x00;
inline constexpr std::uint8_t kCheckpointRedo = 0x10;
}  // namespace system

namespace transaction {
inline constexpr std::uint8_t kCommit = 0x00;
inline constexpr std::uint8_t kAbort = 0x10;
}  // namespace transaction

namespace heap {
inline constexpr std::uint8_t kInsert = 0x00;
inline constexpr std::uint8_t kDelete = 0x10;
inline constexpr std::uint8_t kUpdate = 0x20;
inline constexpr std::uint8_t kHotUpdate = 0x30;
inline constexpr std::uint8_t kLock = 0x40;
inline constexpr std::uint8_t kPrune = 0x50;
inline constexpr std::uint8_t kMultiInsert = 0x60;
inline constexpr std::uint8_t kInitialize = 0x70;
}  // namespace heap

namespace btree {
inline constexpr std::uint8_t kInsertLeaf = 0x00;
inline constexpr std::uint8_t kInsertUpper = 0x10;
inline constexpr std::uint8_t kMetadata = 0x20;
inline constexpr std::uint8_t kSplit = 0x30;
inline constexpr std::uint8_t kFinishSplit = 0x40;
inline constexpr std::uint8_t kDelete = 0x50;
inline constexpr std::uint8_t kSplitFinish = 0x60;
inline constexpr std::uint8_t kNewRoot = 0x80;
inline constexpr std::uint8_t kVacuum = 0x90;
inline constexpr std::uint8_t kBuildPage = 0xA0;
}  // namespace btree

namespace catalog {
inline constexpr std::uint8_t kCreate = 0x00;
}  // namespace catalog

}  // namespace opcode

inline constexpr std::uint16_t kWalBlockHasImage = 1U << 0U;
inline constexpr std::uint16_t kWalBlockHasDelta = 1U << 1U;
inline constexpr std::uint16_t kWalBlockWillInitialize = 1U << 2U;
inline constexpr std::uint16_t kWalBlockStandardPage = 1U << 3U;
inline constexpr std::uint16_t kWalBlockKnownFlags =
    kWalBlockHasImage | kWalBlockHasDelta | kWalBlockWillInitialize | kWalBlockStandardPage;
inline constexpr std::size_t kMaximumWalBlockReferences = 256;

// Optional byte range omitted from a full-page image on disk and restored as
// zeroes during decode. WAL validates only this generic byte-range contract;
// the resource manager validates that it agrees with its page format.
struct WalPageImageHole final {
    std::uint16_t offset{0};
    std::uint16_t length{0};

    constexpr auto empty() const noexcept -> bool {
        return length == 0;
    }

    auto operator==(const WalPageImageHole&) const noexcept -> bool = default;
};

struct WalBlockReference final {
    base::RelationId relation_id{base::kInvalidRelationId};
    base::PageId page_id{base::kInvalidPageId};
    bool will_initialize{false};
    bool standard_page{false};
    std::optional<WalPageImage> full_page_image;
    WalPageImageHole image_hole{};
    // Resource-manager-owned redo bytes. The WAL layer frames and checksums
    // them but never interprets heap, B+Tree, or catalog semantics.
    std::vector<std::byte> delta;

    auto operator==(const WalBlockReference&) const -> bool = default;
};

struct WalRecord final {
    base::Lsn previous_lsn{base::kInvalidLsn};
    base::Xid xid{base::kInvalidXid};
    WalResourceManager resource_manager{WalResourceManager::kSystem};
    std::uint8_t info{0};
    std::vector<WalBlockReference> blocks;
    std::vector<std::byte> main_payload;

    auto operator==(const WalRecord&) const -> bool = default;
};

auto encode_record(const WalRecord& record) -> base::Result<std::vector<std::byte>>;
auto encode_record_into(const WalRecord& record, std::vector<std::byte>& output) -> base::Status;
auto decode_record(base::Bytes bytes) -> base::Result<WalRecord>;
// header must be the result of decoding bytes.first(kWalRecordHeaderSize).
// The split API lets a streaming reader use the length-bearing header to read
// the body and then finish decoding without parsing the header again.
auto decode_record(base::Bytes bytes, const WalRecordHeader& header) -> base::Result<WalRecord>;

}  // namespace db::wal
