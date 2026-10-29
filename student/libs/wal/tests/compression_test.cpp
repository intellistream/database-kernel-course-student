#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/wal/compression.hpp"

namespace db::wal {
namespace {

TEST(WalCompressionTest, RepetitiveBytesUseRleAndRoundTripExactly) {
    std::vector<std::byte> input(700, std::byte{0x11});
    for (std::size_t index = 200; index < 300; ++index) {
        input[index] = static_cast<std::byte>(index);
    }
    const auto compressed = compress_adaptively(input);
    EXPECT_EQ(compressed.method, CompressionMethod::kRle);
    EXPECT_LE(compressed.bytes.size() + kWalCompressionMinimumSavings, input.size());
    EXPECT_EQ(decompress(compressed.method, compressed.bytes, input.size(), 4096).value(), input);
}

TEST(WalCompressionTest, IncompressibleBytesRemainRaw) {
    std::vector<std::byte> input(4096);
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = static_cast<std::byte>((index * 31U + 7U) & 0xFFU);
    }
    const auto compressed = compress_adaptively(input);
    EXPECT_EQ(compressed.method, CompressionMethod::kNone);
    EXPECT_EQ(compressed.bytes, input);
    EXPECT_EQ(decompress(compressed.method, compressed.bytes, input.size(), 4096).value(), input);
}

TEST(WalCompressionTest, RejectsTruncationExpansionAndUnknownMethods) {
    const std::vector<std::byte> truncated_run{std::byte{0x80}};
    EXPECT_EQ(
        decompress(CompressionMethod::kRle, truncated_run, 3, 3).error().code(),
        base::ErrorCode::kCorruption
    );
    const std::vector<std::byte> oversized_run{std::byte{0xFF}, std::byte{1}};
    EXPECT_FALSE(decompress(CompressionMethod::kRle, oversized_run, 4, 4));
    const std::vector<std::byte> short_literal{std::byte{2}, std::byte{1}, std::byte{2}};
    EXPECT_FALSE(decompress(CompressionMethod::kRle, short_literal, 3, 3));
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- corrupt wire method.
    EXPECT_FALSE(decompress(static_cast<CompressionMethod>(77), {}, 0, 16));
    EXPECT_FALSE(decompress(CompressionMethod::kNone, {}, 17, 16));
}

}  // namespace
}  // namespace db::wal
