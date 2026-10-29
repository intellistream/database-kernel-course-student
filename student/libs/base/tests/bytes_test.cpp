#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"

namespace db::base {
namespace {

TEST(BytesTest, WritesAndReadsLittleEndianFixedWidths) {
    std::array<std::byte, 27> storage{};
    LittleEndianWriter writer{storage};

    ASSERT_TRUE(writer.write_u8(0x12));
    ASSERT_TRUE(writer.write_u16(0x3456));
    ASSERT_TRUE(writer.write_u32(0x789ABCDE));
    ASSERT_TRUE(writer.write_u64(0x0123456789ABCDEF));
    ASSERT_TRUE(writer.write_i32(-123456));
    ASSERT_TRUE(writer.write_i64(std::numeric_limits<std::int64_t>::min()));
    EXPECT_EQ(writer.position(), storage.size());
    EXPECT_EQ(storage[1], std::byte{0x56});
    EXPECT_EQ(storage[2], std::byte{0x34});
    EXPECT_EQ(storage[3], std::byte{0xDE});

    LittleEndianReader reader{storage};
    EXPECT_EQ(*reader.read_u8(), 0x12);
    EXPECT_EQ(*reader.read_u16(), 0x3456);
    EXPECT_EQ(*reader.read_u32(), 0x789ABCDEU);
    EXPECT_EQ(*reader.read_u64(), 0x0123456789ABCDEFULL);
    EXPECT_EQ(*reader.read_i32(), -123456);
    EXPECT_EQ(*reader.read_i64(), std::numeric_limits<std::int64_t>::min());
    EXPECT_TRUE(reader.finish());
}

TEST(BytesTest, WritesAndReadsBigEndianFixedWidths) {
    std::array<std::byte, 14> storage{};
    BigEndianWriter writer{storage};

    ASSERT_TRUE(writer.write_u16(0x1234));
    ASSERT_TRUE(writer.write_u32(0x56789ABC));
    ASSERT_TRUE(writer.write_u64(0xDEF00123456789AB));
    EXPECT_EQ(storage[0], std::byte{0x12});
    EXPECT_EQ(storage[1], std::byte{0x34});
    EXPECT_EQ(storage[2], std::byte{0x56});

    BigEndianReader reader{storage};
    EXPECT_EQ(*reader.read_u16(), 0x1234);
    EXPECT_EQ(*reader.read_u32(), 0x56789ABCU);
    EXPECT_EQ(*reader.read_u64(), 0xDEF00123456789ABULL);
    EXPECT_TRUE(reader.finish());
}

TEST(BytesTest, AppendersExtendCallerOwnedStorageInExplicitByteOrder) {
    std::vector<std::byte> little_bytes{std::byte{0xAA}};
    LittleEndianAppender little{little_bytes};
    little.append_u16(0x1234U);
    little.append_i32(-2);
    constexpr std::array suffix{std::byte{0xAA}};
    little.append_bytes(suffix);
    EXPECT_EQ(
        little_bytes,
        std::vector<std::byte>({
            std::byte{0xAA},
            std::byte{0x34},
            std::byte{0x12},
            std::byte{0xFE},
            std::byte{0xFF},
            std::byte{0xFF},
            std::byte{0xFF},
            std::byte{0xAA},
        })
    );

    std::vector<std::byte> big_bytes;
    BigEndianAppender big{big_bytes};
    big.append_u32(0x1234'5678U);
    EXPECT_EQ(
        big_bytes,
        std::vector<std::byte>({
            std::byte{0x12},
            std::byte{0x34},
            std::byte{0x56},
            std::byte{0x78},
        })
    );
}

TEST(BytesTest, PreservesCursorWhenInputIsTruncated) {
    constexpr std::array input{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    LittleEndianReader reader{input};

    const Result<std::uint32_t> value = reader.read_u32();

    ASSERT_FALSE(value);
    EXPECT_EQ(value.error().code(), ErrorCode::kCorruption);
    EXPECT_EQ(reader.position(), 0U);
    EXPECT_EQ(reader.remaining(), input.size());
}

TEST(BytesTest, PreservesCursorWhenOutputIsTooSmall) {
    std::array<std::byte, 3> output{};
    BigEndianWriter writer{output};

    const Status status = writer.write_u32(42);

    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code(), ErrorCode::kOutOfRange);
    EXPECT_EQ(writer.position(), 0U);
    EXPECT_EQ(writer.remaining(), output.size());
}

TEST(BytesTest, ReadsAndWritesBoundedByteSpans) {
    constexpr std::array payload{std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
    std::array<std::byte, 4> output{};
    LittleEndianWriter writer{output};

    ASSERT_TRUE(writer.write_u8(3));
    ASSERT_TRUE(writer.write_bytes(payload));
    EXPECT_EQ(writer.written().size(), output.size());

    LittleEndianReader reader{output};
    EXPECT_EQ(*reader.read_u8(), 3);
    const Result<Bytes> decoded = reader.read_bytes(3);
    ASSERT_TRUE(decoded);
    EXPECT_TRUE(std::ranges::equal(*decoded, payload));
    EXPECT_TRUE(reader.finish());
}

TEST(BytesTest, RejectsTrailingAndOversizedByteInput) {
    constexpr std::array input{std::byte{0x01}, std::byte{0x02}};
    LittleEndianReader reader{input};

    EXPECT_EQ(*reader.read_u8(), 1);
    const Status trailing = reader.finish();
    const Result<Bytes> oversized = reader.read_bytes(2);

    ASSERT_FALSE(trailing);
    ASSERT_FALSE(oversized);
    EXPECT_EQ(trailing.error().code(), ErrorCode::kCorruption);
    EXPECT_EQ(oversized.error().code(), ErrorCode::kCorruption);
    EXPECT_EQ(reader.position(), 1U);
}

}  // namespace
}  // namespace db::base
