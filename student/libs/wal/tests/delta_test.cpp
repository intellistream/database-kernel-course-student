#include <array>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/wal/delta.hpp"

namespace db::wal {
namespace {

TEST(PageDeltaTest, BuildsEncodesDecodesAndAppliesChangedRuns) {
    WalPageImage before{};
    auto after = before;
    after[17] = std::byte{1};
    after[18] = std::byte{2};
    after[2048] = std::byte{0xA5};
    after.back() = std::byte{0x5A};

    const auto patches = make_page_delta(before, after);
    ASSERT_TRUE(patches);
    ASSERT_EQ(patches->size(), 3U);
    EXPECT_EQ(patches->patches().front().offset, 17);
    const auto encoded = encode_page_delta(patches.value());
    ASSERT_TRUE(encoded);
    const auto decoded = decode_page_delta(encoded.value());
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value(), patches.value());

    auto applied = before;
    ASSERT_TRUE(apply_page_delta(applied, decoded.value()));
    EXPECT_EQ(applied, after);
}

TEST(PageDeltaTest, EmptyDeltaIsCanonicalAndNoOp) {
    WalPageImage page{};
    EXPECT_TRUE(make_page_delta(page, page)->empty());
    EXPECT_TRUE(encode_page_delta(std::span<const PageDeltaPatch>{})->empty());
    EXPECT_TRUE(decode_page_delta({})->empty());
    EXPECT_TRUE(apply_page_delta(page, std::span<const PageDeltaPatch>{}));
}

TEST(PageDeltaTest, RejectsOverlapsBoundsTruncationAndTrailingBytes) {
    const std::vector<PageDeltaPatch> overlap{
        PageDeltaPatch{.offset = 10, .bytes = {std::byte{1}, std::byte{2}}},
        PageDeltaPatch{.offset = 11, .bytes = {std::byte{3}}},
    };
    EXPECT_EQ(encode_page_delta(overlap).error().code(), base::ErrorCode::kInvalidArgument);
    const std::vector<PageDeltaPatch> outside{
        PageDeltaPatch{.offset = 4095, .bytes = {std::byte{1}, std::byte{2}}},
    };
    EXPECT_FALSE(encode_page_delta(outside));

    const std::array<std::byte, 5> truncated{
        std::byte{1},
        std::byte{0},
        std::byte{4},
        std::byte{0},
        std::byte{2},
    };
    EXPECT_FALSE(decode_page_delta(truncated));
    const std::array<std::byte, 3> zero_count{std::byte{0}, std::byte{0}, std::byte{1}};
    EXPECT_FALSE(decode_page_delta(zero_count));

    std::array<std::byte, 8> short_page{};
    EXPECT_FALSE(apply_page_delta(short_page, outside));
}

}  // namespace
}  // namespace db::wal
