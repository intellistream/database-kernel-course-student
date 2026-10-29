#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"

namespace db::base {
namespace {

auto bytes_of(const std::string_view value) -> Bytes {
    return std::as_bytes(std::span{value.data(), value.size()});
}

auto reference_crc32c(const Bytes bytes) -> std::uint32_t {
    constexpr std::uint32_t polynomial = 0x82F63B78U;
    auto checksum = std::numeric_limits<std::uint32_t>::max();
    for (const auto byte : bytes) {
        checksum ^= std::to_integer<std::uint8_t>(byte);
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = 0U - (checksum & 1U);
            checksum = (checksum >> 1U) ^ (polynomial & mask);
        }
    }
    return ~checksum;
}

TEST(Crc32cTest, MatchesTheCastagnoliReferenceVector) {
    EXPECT_EQ(crc32c(bytes_of("123456789")), 0xE3069283U);
    EXPECT_EQ(crc32c({}), 0U);
}

TEST(Crc32cTest, ComputesWithAnInteriorRegionTreatedAsZero) {
    std::array<std::byte, 11> bytes{
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
        std::byte{5},
        std::byte{6},
        std::byte{7},
        std::byte{8},
        std::byte{9},
        std::byte{10},
        std::byte{11},
    };
    auto zeroed = bytes;
    zeroed[4] = std::byte{0};
    zeroed[5] = std::byte{0};
    zeroed[6] = std::byte{0};
    EXPECT_EQ(crc32c_with_zeroed(bytes, 4, 3).value(), crc32c(zeroed));
    EXPECT_EQ(
        crc32c_with_zeroed(bytes, bytes.size(), 1).error().code(),
        ErrorCode::kInvalidArgument
    );
}

TEST(Crc32cTest, MatchesIndependentBitwiseOracleAcrossChunkBoundaries) {
    constexpr std::array<std::size_t, 13>
        lengths{0, 1, 7, 8, 9, 15, 16, 17, 31, 100, 4095, 4096, 4097};
    for (const auto length : lengths) {
        std::vector<std::byte> bytes(length);
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            bytes[index] = static_cast<std::byte>(((index * 7U) + 3U) & 0xFFU);
        }
        EXPECT_EQ(crc32c(bytes), reference_crc32c(bytes)) << "length " << length;
    }
}

TEST(Crc32cTest, SealsAndVerifiesALittleEndianField) {
    std::array<std::byte, 32> bytes{};
    bytes.front() = std::byte{0xA5};
    bytes.back() = std::byte{0x5A};
    std::ranges::fill(std::span{bytes}.subspan(12U, kCrc32cSize), std::byte{0xFF});
    ASSERT_TRUE(seal_crc32c(bytes, 12));
    EXPECT_TRUE(verify_crc32c(bytes, 12));
    bytes.back() ^= std::byte{1};
    const auto corrupt = verify_crc32c(bytes, 12);
    ASSERT_FALSE(corrupt);
    EXPECT_EQ(corrupt.error().code(), ErrorCode::kCorruption);
    EXPECT_FALSE(seal_crc32c(bytes, bytes.size() - 3));
}

}  // namespace
}  // namespace db::base
