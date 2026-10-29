#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/wal/format.hpp"

namespace db::wal {
namespace {

auto read_u16_at(const base::Bytes bytes, const std::size_t offset) -> std::uint16_t {
    base::LittleEndianReader reader{bytes.subspan(offset, sizeof(std::uint16_t))};
    return reader.read_u16().value();
}

auto read_u32_at(const base::Bytes bytes, const std::size_t offset) -> std::uint32_t {
    base::LittleEndianReader reader{bytes.subspan(offset, sizeof(std::uint32_t))};
    return reader.read_u32().value();
}

auto read_u64_at(const base::Bytes bytes, const std::size_t offset) -> std::uint64_t {
    base::LittleEndianReader reader{bytes.subspan(offset, sizeof(std::uint64_t))};
    return reader.read_u64().value();
}

TEST(WalFormatTest, RecordHeaderUsesStableLittleEndianOffsets) {
    const WalRecordHeader header{
        .total_length = 112,
        .payload_length = 24,
        .previous_lsn = base::Lsn{0x0102'0304'0506'0708ULL},
        .xid = base::Xid{0x1112'1314U},
        .resource_manager = WalResourceManager::kHeap,
        .info = 0x21,
        .block_count = 2,
        .flags = kWalRecordHasFullPageImage | kWalRecordHasDelta,
        .checksum = 0xA1B2'C3D4U,
    };
    const auto encoded = encode_record_header(header);
    ASSERT_TRUE(encoded);
    const base::Bytes bytes{encoded.value()};
    EXPECT_EQ(read_u32_at(bytes, 0), kWalRecordMagic);
    EXPECT_EQ(read_u16_at(bytes, 4), kWalFormatVersion);
    EXPECT_EQ(read_u16_at(bytes, 6), kWalRecordHeaderSize);
    EXPECT_EQ(read_u32_at(bytes, 8), 112U);
    EXPECT_EQ(read_u32_at(bytes, 12), 24U);
    EXPECT_EQ(read_u64_at(bytes, 16), header.previous_lsn.value());
    EXPECT_EQ(read_u32_at(bytes, 24), header.xid.value());
    EXPECT_EQ(read_u16_at(bytes, 30), 2U);
    EXPECT_EQ(read_u16_at(bytes, 32), header.flags);
    EXPECT_EQ(read_u32_at(bytes, kWalRecordChecksumOffset), header.checksum);
    EXPECT_EQ(decode_record_header(bytes).value(), header);
}

TEST(WalFormatTest, PageHeaderBindsAlignedLsnToSegment) {
    const WalPageHeader header{
        .page_start_lsn = base::Lsn{kWalSegmentSize + kWalPageSize},
        .segment_number = 1,
        .flags = kWalPageContinuation,
        .checksum = 0x0102'0304U,
    };
    const auto encoded = encode_page_header(header);
    ASSERT_TRUE(encoded);
    const base::Bytes bytes{encoded.value()};
    EXPECT_EQ(read_u32_at(bytes, 0), kWalPageMagic);
    EXPECT_EQ(read_u64_at(bytes, 8), header.page_start_lsn.value());
    EXPECT_EQ(read_u64_at(bytes, 16), 1U);
    EXPECT_EQ(read_u32_at(bytes, kWalPageChecksumOffset), header.checksum);
    EXPECT_EQ(decode_page_header(bytes).value(), header);

    auto invalid = header;
    invalid.page_start_lsn = base::Lsn{header.page_start_lsn.value() + 1U};
    EXPECT_FALSE(encode_page_header(invalid));
}

TEST(WalFormatTest, SegmentHeaderFixesGeometryAndIdentity) {
    const WalSegmentHeader header{
        .system_identifier = 0x0102'0304'0506'0708ULL,
        .segment_number = 3,
        .first_lsn = base::Lsn{3U * kWalSegmentSize},
        .timeline = 7,
        .checksum = 0x1122'3344U,
    };
    const auto encoded = encode_segment_header(header);
    ASSERT_TRUE(encoded);
    const base::Bytes bytes{encoded.value()};
    EXPECT_EQ(read_u64_at(bytes, 0), kWalSegmentMagic);
    EXPECT_EQ(read_u32_at(bytes, 12), kWalPageSize);
    EXPECT_EQ(read_u64_at(bytes, 16), kWalSegmentSize);
    EXPECT_EQ(read_u64_at(bytes, 24), header.system_identifier);
    EXPECT_EQ(read_u64_at(bytes, 32), 3U);
    EXPECT_EQ(read_u64_at(bytes, 40), header.first_lsn.value());
    EXPECT_EQ(read_u32_at(bytes, kWalSegmentChecksumOffset), header.checksum);
    EXPECT_EQ(decode_segment_header(bytes).value(), header);
}

TEST(WalFormatTest, ControlImageUsesStableFieldsAndZeroTail) {
    const WalControlImage control{
        .system_identifier = 0x1112'1314'1516'1718ULL,
        .generation = 9,
        .checkpoint_lsn = base::Lsn{4096},
        .redo_lsn = base::Lsn{2048},
        .next_xid = base::Xid{77},
        .latest_completed_xid = base::Xid{75},
        .next_object_id = 42,
        .state = DatabaseState::kInProduction,
        .checksum = 0x5566'7788U,
    };
    const auto encoded = encode_control_image(control);
    ASSERT_TRUE(encoded);
    const base::Bytes bytes{encoded.value()};
    EXPECT_EQ(read_u64_at(bytes, 0), kWalControlMagic);
    EXPECT_EQ(read_u16_at(bytes, 10), kWalControlHeaderSize);
    EXPECT_EQ(read_u32_at(bytes, 12), kWalControlFileSize);
    EXPECT_EQ(read_u64_at(bytes, 16), control.system_identifier);
    EXPECT_EQ(read_u64_at(bytes, 24), control.generation);
    EXPECT_EQ(read_u64_at(bytes, 32), control.checkpoint_lsn.value());
    EXPECT_EQ(read_u64_at(bytes, 40), control.redo_lsn.value());
    EXPECT_EQ(read_u32_at(bytes, 48), control.next_xid.value());
    EXPECT_EQ(read_u32_at(bytes, kWalControlChecksumOffset), control.checksum);
    EXPECT_EQ(decode_control_image(bytes).value(), control);
}

TEST(WalFormatTest, DecodersRejectWrongSizesMagicVersionsAndReservedBytes) {
    auto record = encode_record_header(WalRecordHeader{}).value();
    EXPECT_FALSE(decode_record_header(base::Bytes{record}.first(record.size() - 1U)));
    record[0] = std::byte{0};
    EXPECT_EQ(decode_record_header(record).error().code(), base::ErrorCode::kCorruption);

    auto segment = encode_segment_header(
                       WalSegmentHeader{
                           .system_identifier = 1,
                           .segment_number = 0,
                           .first_lsn = base::Lsn{0},
                       }
    )
                       .value();
    segment[8] = std::byte{2};
    EXPECT_EQ(decode_segment_header(segment).error().code(), base::ErrorCode::kUnsupported);

    auto control = encode_control_image(WalControlImage{.system_identifier = 1}).value();
    control[kWalControlHeaderSize] = std::byte{1};
    EXPECT_EQ(decode_control_image(control).error().code(), base::ErrorCode::kCorruption);
}

TEST(WalFormatTest, EncodersRejectInconsistentRecordSegmentAndControlState) {
    EXPECT_FALSE(encode_record_header(
        WalRecordHeader{
            .total_length = kWalRecordHeaderSize,
            .payload_length = 1,
        }
    ));
    EXPECT_FALSE(encode_segment_header(
        WalSegmentHeader{
            .system_identifier = 1,
            .segment_number = 2,
            .first_lsn = base::Lsn{kWalSegmentSize},
        }
    ));
    EXPECT_FALSE(encode_control_image(
        WalControlImage{
            .system_identifier = 1,
            .checkpoint_lsn = base::kInvalidLsn,
            .redo_lsn = base::Lsn{1},
        }
    ));
}

}  // namespace
}  // namespace db::wal
