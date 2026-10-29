#include <cstdint>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/types/datetime.hpp"

namespace db::types {
namespace {

TEST(DateTimeTest, ParsesAndFormatsPostgresEpochAndLeapDay) {
    const auto epoch = parse_timestamp("2000-01-01 00:00:00");
    const auto leap = parse_timestamp("2024-02-29T23:59:59.123456");

    ASSERT_TRUE(epoch);
    EXPECT_EQ(epoch.value(), TimestampValue{0});
    ASSERT_TRUE(leap);
    EXPECT_EQ(format_timestamp(leap.value()), "2024-02-29 23:59:59.123456");
}

TEST(DateTimeTest, RoundsLongFractionsAtBinaryMicrosecondBoundary) {
    const auto rounded = parse_timestamp("2000-01-01 00:00:00.1234567");
    const auto carried = parse_timestamp("2000-01-01 00:00:00.9999995");

    ASSERT_TRUE(rounded);
    EXPECT_EQ(rounded.value().microseconds(), 123'457);
    ASSERT_TRUE(carried);
    EXPECT_EQ(carried.value().microseconds(), 1'000'000);
    EXPECT_EQ(format_timestamp(carried.value()), "2000-01-01 00:00:01");
}

TEST(DateTimeTest, FormatsNegativeEpochValuesWithEuclideanTime) {
    EXPECT_EQ(format_timestamp(TimestampValue{-1}), "1999-12-31 23:59:59.999999");
    EXPECT_EQ(format_timestamp(TimestampValue{-86'400'000'000LL}), "1999-12-31 00:00:00");
}

TEST(DateTimeTest, RejectsInvalidCalendarTimeAndTimezoneInputs) {
    const auto invalid_day = parse_timestamp("2023-02-29 00:00:00");
    const auto invalid_time = parse_timestamp("2000-01-01 24:00:00");
    const auto timezone = parse_timestamp("2000-01-01 00:00:00Z");
    const auto missing = parse_timestamp("2000-01-01");
    const auto bad_fraction = parse_timestamp("2000-01-01 00:00:00.x");

    EXPECT_FALSE(invalid_day);
    EXPECT_FALSE(invalid_time);
    EXPECT_FALSE(timezone);
    EXPECT_FALSE(missing);
    EXPECT_FALSE(bad_fraction);
}

TEST(DateTimeTest, AppliesTimestampPrecisionSymmetrically) {
    const auto positive = adjust_timestamp_precision(TimestampValue{1'550'000}, 1);
    const auto negative = adjust_timestamp_precision(TimestampValue{-1'550'000}, 1);
    const auto invalid = adjust_timestamp_precision(TimestampValue{0}, 7);
    const auto overflow =
        adjust_timestamp_precision(TimestampValue{std::numeric_limits<std::int64_t>::min()}, 0);

    ASSERT_TRUE(positive);
    EXPECT_EQ(positive.value().microseconds(), 1'600'000);
    ASSERT_TRUE(negative);
    EXPECT_EQ(negative.value().microseconds(), -1'600'000);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code(), base::ErrorCode::kOutOfRange);
}

}  // namespace
}  // namespace db::types
