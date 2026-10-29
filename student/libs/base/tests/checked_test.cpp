#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "db/base/checked.hpp"

namespace db::base {
namespace {

TEST(CheckedArithmeticTest, AddsAndSubtractsWithinRange) {
    const Result<std::uint32_t> sum = checked_add<std::uint32_t>(20, 22);
    const Result<std::int32_t> negative_sum = checked_add<std::int32_t>(-20, -22);
    const Result<std::uint32_t> difference = checked_subtract<std::uint32_t>(50, 8);
    const Result<std::int32_t> signed_difference = checked_subtract<std::int32_t>(-20, 22);

    ASSERT_TRUE(sum);
    ASSERT_TRUE(negative_sum);
    ASSERT_TRUE(difference);
    ASSERT_TRUE(signed_difference);
    EXPECT_EQ(*sum, 42U);
    EXPECT_EQ(*negative_sum, -42);
    EXPECT_EQ(*difference, 42U);
    EXPECT_EQ(*signed_difference, -42);
}

TEST(CheckedArithmeticTest, RejectsAdditionAndSubtractionOverflow) {
    const auto uint_add = checked_add(std::numeric_limits<std::uint32_t>::max(), 1U);
    const auto int_add = checked_add(std::numeric_limits<std::int32_t>::max(), 1);
    const auto uint_subtract = checked_subtract(0U, 1U);
    const auto int_subtract = checked_subtract(std::numeric_limits<std::int32_t>::min(), 1);

    ASSERT_FALSE(uint_add);
    ASSERT_FALSE(int_add);
    ASSERT_FALSE(uint_subtract);
    ASSERT_FALSE(int_subtract);
    EXPECT_EQ(uint_add.error().code(), ErrorCode::kOutOfRange);
    EXPECT_EQ(int_add.error().code(), ErrorCode::kOutOfRange);
    EXPECT_EQ(uint_subtract.error().code(), ErrorCode::kOutOfRange);
    EXPECT_EQ(int_subtract.error().code(), ErrorCode::kOutOfRange);
}

TEST(CheckedArithmeticTest, MultipliesWithoutUndefinedOverflow) {
    const auto positive = checked_multiply<std::int32_t>(6, 7);
    const auto negative = checked_multiply<std::int32_t>(-6, 7);
    const auto uint_overflow =
        checked_multiply(std::numeric_limits<std::uint64_t>::max(), std::uint64_t{2});
    const auto positive_overflow = checked_multiply(std::numeric_limits<std::int32_t>::max(), 2);
    const auto negative_overflow = checked_multiply(std::numeric_limits<std::int32_t>::min(), -1);

    ASSERT_TRUE(positive);
    ASSERT_TRUE(negative);
    EXPECT_EQ(*positive, 42);
    EXPECT_EQ(*negative, -42);
    EXPECT_EQ(uint_overflow.error().code(), ErrorCode::kOutOfRange);
    EXPECT_EQ(positive_overflow.error().code(), ErrorCode::kOutOfRange);
    EXPECT_EQ(negative_overflow.error().code(), ErrorCode::kOutOfRange);
}

TEST(CheckedArithmeticTest, CastsOnlyRepresentableIntegers) {
    const auto narrowed = checked_cast<std::uint16_t>(std::uint32_t{65535});
    const auto too_large = checked_cast<std::uint16_t>(std::uint32_t{65536});
    const auto negative = checked_cast<std::uint32_t>(std::int32_t{-1});
    const auto signed_value = checked_cast<std::int32_t>(std::uint32_t{42});

    ASSERT_TRUE(narrowed);
    ASSERT_TRUE(signed_value);
    EXPECT_EQ(*narrowed, 65535U);
    EXPECT_EQ(*signed_value, 42);
    EXPECT_EQ(too_large.error().code(), ErrorCode::kOutOfRange);
    EXPECT_EQ(negative.error().code(), ErrorCode::kOutOfRange);
}

}  // namespace
}  // namespace db::base
