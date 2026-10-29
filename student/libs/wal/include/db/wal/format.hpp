#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/wal/control.hpp"
#include "db/wal/geometry.hpp"
#include "db/wal/resource_manager.hpp"

namespace db::wal {

inline constexpr std::uint16_t kWalFormatVersion = 1;

inline constexpr std::uint32_t kWalRecordMagic = 0x31435257U;  // "WRC1"
inline constexpr std::uint32_t kWalPageMagic = 0x31475057U;  // "WPG1"
inline constexpr std::uint64_t kWalSegmentMagic = 0x0031474553525746ULL;  // "FWRSEG1\0"
inline constexpr std::uint64_t kWalControlMagic = 0x00314C5443525746ULL;  // "FWRCTL1\0"

inline constexpr std::size_t kWalRecordHeaderSize = 40;
inline constexpr std::size_t kWalBlockHeaderSize = 24;
inline constexpr std::size_t kWalPageHeaderSize = 32;
inline constexpr std::size_t kWalSegmentHeaderSize = 64;
inline constexpr std::size_t kWalControlHeaderSize = 72;
inline constexpr std::size_t kWalControlFileSize = 512;

inline constexpr std::size_t kWalRecordChecksumOffset = 36;
inline constexpr std::size_t kWalPageChecksumOffset = 28;
inline constexpr std::size_t kWalSegmentChecksumOffset = 52;
inline constexpr std::size_t kWalControlChecksumOffset = 64;

inline constexpr std::uint16_t kWalRecordHasFullPageImage = 1U << 0U;
inline constexpr std::uint16_t kWalRecordHasDelta = 1U << 1U;
inline constexpr std::uint16_t kWalRecordPadding = 1U << 2U;
inline constexpr std::uint16_t kWalRecordKnownFlags =
    kWalRecordHasFullPageImage | kWalRecordHasDelta | kWalRecordPadding;

inline constexpr std::uint16_t kWalPageContinuation = 1U << 0U;
inline constexpr std::uint16_t kWalPageKnownFlags = kWalPageContinuation;

struct WalRecordHeader final {
    std::uint32_t total_length{kWalRecordHeaderSize};
    std::uint32_t payload_length{0};
    base::Lsn previous_lsn{base::kInvalidLsn};
    base::Xid xid{base::kInvalidXid};
    WalResourceManager resource_manager{WalResourceManager::kSystem};
    std::uint8_t info{0};
    std::uint16_t block_count{0};
    std::uint16_t flags{0};
    std::uint32_t checksum{0};

    auto operator==(const WalRecordHeader&) const noexcept -> bool = default;
};

struct WalPageHeader final {
    base::Lsn page_start_lsn{base::kInvalidLsn};
    std::uint64_t segment_number{0};
    std::uint16_t flags{0};
    std::uint32_t checksum{0};

    auto operator==(const WalPageHeader&) const noexcept -> bool = default;
};

struct WalSegmentHeader final {
    std::uint64_t system_identifier{0};
    std::uint64_t segment_number{0};
    base::Lsn first_lsn{base::kInvalidLsn};
    std::uint32_t timeline{1};
    std::uint32_t checksum{0};

    auto operator==(const WalSegmentHeader&) const noexcept -> bool = default;
};

using WalRecordHeaderBytes = std::array<std::byte, kWalRecordHeaderSize>;
using WalPageHeaderBytes = std::array<std::byte, kWalPageHeaderSize>;
using WalSegmentHeaderBytes = std::array<std::byte, kWalSegmentHeaderSize>;
using WalControlFileBytes = std::array<std::byte, kWalControlFileSize>;

auto encode_record_header(const WalRecordHeader& header) -> base::Result<WalRecordHeaderBytes>;
auto decode_record_header(base::Bytes bytes) -> base::Result<WalRecordHeader>;

auto encode_page_header(const WalPageHeader& header) -> base::Result<WalPageHeaderBytes>;
auto decode_page_header(base::Bytes bytes) -> base::Result<WalPageHeader>;

auto encode_segment_header(const WalSegmentHeader& header) -> base::Result<WalSegmentHeaderBytes>;
auto decode_segment_header(base::Bytes bytes) -> base::Result<WalSegmentHeader>;

auto encode_control_image(const WalControlImage& control) -> base::Result<WalControlFileBytes>;
auto decode_control_image(base::Bytes bytes) -> base::Result<WalControlImage>;

}  // namespace db::wal
