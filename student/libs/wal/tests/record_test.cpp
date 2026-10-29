#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/wal/compression.hpp"
#include "db/wal/delta.hpp"
#include "db/wal/format.hpp"
#include "db/wal/record.hpp"

namespace db::wal {
namespace {

inline constexpr base::RelationId kRelationId{9U};
inline constexpr base::PageId kPageId{4U};
inline constexpr WalPageImageHole kStandardPageHole{.offset = 32U, .length = 4000U};

auto byte_vector(const std::string_view value) -> std::vector<std::byte> {
    const auto bytes = std::as_bytes(std::span{value.data(), value.size()});
    return {bytes.begin(), bytes.end()};
}

auto standard_page_image() -> WalPageImage {
    WalPageImage image{};
    for (std::size_t index = 0U; index < kStandardPageHole.offset; ++index) {
        image.at(index) = static_cast<std::byte>((index * 7U) & 0xFFU);
    }
    image.at(4032U) = std::byte{0xA5};
    image.back() = std::byte{0x5A};
    return image;
}

auto sample_record() -> WalRecord {
    return WalRecord{
        .previous_lsn = base::Lsn{80},
        .xid = base::Xid{17},
        .resource_manager = WalResourceManager::kHeap,
        .info = 3,
        .blocks =
            {
                WalBlockReference{
                    .relation_id = kRelationId,
                    .page_id = kPageId,
                    .standard_page = true,
                    .full_page_image = standard_page_image(),
                    .image_hole = kStandardPageHole,
                    .delta = encode_page_delta(
                                 std::array{
                                     PageDeltaPatch{
                                         .offset = 4050,
                                         .bytes = {std::byte{1}, std::byte{2}}
                                     },
                                 }
                    )
                                 .value(),
                },
                WalBlockReference{
                    .relation_id = base::RelationId{9},
                    .page_id = base::PageId{5},
                    .will_initialize = true,
                    .standard_page = false,
                    .full_page_image = std::nullopt,
                    .delta =
                        encode_page_delta(
                            std::array{
                                PageDeltaPatch{.offset = 32, .bytes = {std::byte{4}, std::byte{5}}},
                            }
                        )
                            .value(),
                },
            },
        .main_payload = byte_vector("split"),
    };
}

auto read_u16_at(const base::Bytes bytes, const std::size_t offset) -> std::uint16_t {
    base::LittleEndianReader reader{bytes.subspan(offset, sizeof(std::uint16_t))};
    return reader.read_u16().value();
}

auto read_u32_at(const base::Bytes bytes, const std::size_t offset) -> std::uint32_t {
    base::LittleEndianReader reader{bytes.subspan(offset, sizeof(std::uint32_t))};
    return reader.read_u32().value();
}

void reseal(std::vector<std::byte>& bytes) {
    ASSERT_TRUE(base::seal_crc32c(bytes, kWalRecordChecksumOffset));
}

auto decode_image(const base::Bytes bytes) -> base::Result<WalRecord> {
    if (bytes.size() < kWalRecordHeaderSize) {
        return base::fail(base::Error{base::ErrorCode::kCorruption, "WAL record is truncated"});
    }
    auto header = decode_record_header(bytes.first(kWalRecordHeaderSize));
    if (!header) {
        return base::fail(std::move(header).error());
    }
    return decode_record(bytes, header.value());
}

TEST(WalRecordTest, MultiBlockRecordUsesStableLayoutAndRoundTrips) {
    const auto record = sample_record();
    const auto encoded = encode_record(record);
    ASSERT_TRUE(encoded);
    const base::Bytes bytes{encoded.value()};
    EXPECT_EQ(bytes.size() % 8U, 0U);
    EXPECT_LT(bytes.size(), kWalDataPageSize);
    EXPECT_EQ(read_u32_at(bytes, 8), bytes.size());
    EXPECT_EQ(read_u16_at(bytes, 30), 2U);
    EXPECT_EQ(read_u32_at(bytes, 40), 9U);
    EXPECT_EQ(read_u32_at(bytes, 44), 4U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(bytes[56]), 0U);
    EXPECT_EQ(
        std::to_integer<std::uint8_t>(bytes[57]),
        static_cast<std::uint8_t>(CompressionMethod::kRle)
    );
    EXPECT_NE(read_u16_at(bytes, 58) & kWalBlockHasImage, 0U);
    EXPECT_NE(read_u16_at(bytes, 58) & kWalBlockHasDelta, 0U);
    EXPECT_NE(read_u16_at(bytes, 58) & kWalBlockStandardPage, 0U);
    EXPECT_TRUE(base::verify_crc32c(bytes, kWalRecordChecksumOffset));
    EXPECT_EQ(decode_image(bytes).value(), record);
}

TEST(WalRecordTest, StandardFullPageImageOmitsAndZeroesTheFreeSpaceHole) {
    auto record = sample_record();
    auto& image = record.blocks.front().full_page_image.value();
    image[128] = std::byte{0xCC};
    const auto encoded = encode_record(record);
    ASSERT_TRUE(encoded);
    const auto decoded = decode_image(encoded.value());
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->blocks.front().full_page_image->at(128), std::byte{0});
    image[128] = std::byte{0};
    EXPECT_EQ(decoded.value(), record);
}

TEST(WalRecordTest, NonStandardIncompressibleImageRemainsRaw) {
    WalPageImage image{};
    for (std::size_t index = 0; index < image.size(); ++index) {
        image.at(index) = static_cast<std::byte>((index * 31U + 7U) & 0xFFU);
    }
    const WalRecord record{
        .xid = base::Xid{1},
        .resource_manager = WalResourceManager::kSystem,
        .blocks =
            {
                WalBlockReference{
                    .relation_id = base::RelationId{1},
                    .page_id = base::PageId{0},
                    .full_page_image = image,
                    .delta = {},
                },
            },
        .main_payload = {},
    };
    const auto encoded = encode_record(record);
    ASSERT_TRUE(encoded);
    EXPECT_EQ(
        std::to_integer<std::uint8_t>(encoded->at(kWalRecordHeaderSize + 17U)),
        static_cast<std::uint8_t>(CompressionMethod::kNone)
    );
    EXPECT_EQ(decode_image(encoded.value()).value(), record);
}

TEST(WalRecordTest, MainOnlyRecordHasCanonicalZeroPadding) {
    const WalRecord record{
        .resource_manager = WalResourceManager::kTransaction,
        .blocks = {},
        .main_payload = {std::byte{0x42}},
    };
    auto encoded = encode_record(record).value();
    EXPECT_EQ(encoded.size(), 48U);
    EXPECT_EQ(decode_image(encoded).value(), record);
    encoded.back() = std::byte{1};
    reseal(encoded);
    EXPECT_EQ(decode_image(encoded).error().code(), base::ErrorCode::kCorruption);
}

TEST(WalRecordTest, ReusableEncoderClearsBytesFromThePreviousRecord) {
    std::vector<std::byte> buffer;
    ASSERT_TRUE(encode_record_into(sample_record(), buffer));

    const WalRecord smaller{
        .resource_manager = WalResourceManager::kTransaction,
        .blocks = {},
        .main_payload = {std::byte{0x42}},
    };
    ASSERT_TRUE(encode_record_into(smaller, buffer));
    const auto canonical = encode_record(smaller);
    ASSERT_TRUE(canonical);
    EXPECT_EQ(buffer, canonical.value());
}

TEST(WalRecordTest, CrcDetectsAnyUnrepairedRecordCorruption) {
    auto encoded = encode_record(sample_record()).value();
    encoded.back() ^= std::byte{1};
    const auto decoded = decode_image(encoded);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code(), base::ErrorCode::kCorruption);
}

TEST(WalRecordTest, RejectsSemanticCorruptionAfterAValidCrc) {
    auto duplicate_id = encode_record(sample_record()).value();
    duplicate_id[kWalRecordHeaderSize + kWalBlockHeaderSize + 16U] = std::byte{0};
    reseal(duplicate_id);
    EXPECT_FALSE(decode_image(duplicate_id));

    auto unknown_flags = encode_record(sample_record()).value();
    unknown_flags[kWalRecordHeaderSize + 19U] |= std::byte{0x80};
    reseal(unknown_flags);
    EXPECT_FALSE(decode_image(unknown_flags));

    auto mismatched_hole = encode_record(sample_record()).value();
    mismatched_hole[kWalRecordHeaderSize + 20U] = std::byte{0xFF};
    reseal(mismatched_hole);
    EXPECT_FALSE(decode_image(mismatched_hole));

    auto malformed_rle = encode_record(sample_record()).value();
    const auto image_start = kWalRecordHeaderSize + 2U * kWalBlockHeaderSize;
    malformed_rle[image_start] = std::byte{0x7F};
    reseal(malformed_rle);
    EXPECT_FALSE(decode_image(malformed_rle));

    auto wrong_record_flags = encode_record(sample_record()).value();
    wrong_record_flags[32] = std::byte{0};
    wrong_record_flags[33] = std::byte{0};
    reseal(wrong_record_flags);
    EXPECT_FALSE(decode_image(wrong_record_flags));
}

TEST(WalRecordTest, EncoderRejectsInvalidAddressesImagesAndBlockCount) {
    EXPECT_FALSE(encode_record(
        WalRecord{
            .blocks = {WalBlockReference{
                .relation_id = base::kInvalidRelationId,
                .page_id = base::kInvalidPageId,
                .will_initialize = false,
                .standard_page = false,
                .full_page_image = std::nullopt,
                .delta = {},
            }},
            .main_payload = {},
        }
    ));
    EXPECT_FALSE(encode_record(
        WalRecord{
            .blocks = {WalBlockReference{
                .relation_id = base::RelationId{1},
                .page_id = base::PageId{1},
                .standard_page = true,
                .full_page_image = WalPageImage{},
                .image_hole = WalPageImageHole{.offset = 4090U, .length = 10U},
                .delta = {},
            }},
            .main_payload = {},
        }
    ));
    WalRecord too_many;
    too_many.blocks.resize(kMaximumWalBlockReferences + 1U);
    EXPECT_FALSE(encode_record(too_many));
}

}  // namespace
}  // namespace db::wal
