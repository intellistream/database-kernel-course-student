#include <string_view>

#include <gtest/gtest.h>

#include "db/base/text.hpp"

namespace db::base {
namespace {

TEST(TextTest, TrimsOnlyAsciiWhitespaceAndFoldsOnlyAsciiCase) {
    EXPECT_EQ(trim_ascii_space(" \t\r\ndb\f\v"), "db");
    constexpr std::string_view non_ascii_space{
        "\xc2\xa0"
        "db"
        "\xc2\xa0",
        10U
    };
    EXPECT_EQ(trim_ascii_space(non_ascii_space), non_ascii_space);
    EXPECT_TRUE(ascii_equal_ignoring_case("Timestamp", "TIMESTAMP"));
    EXPECT_FALSE(ascii_equal_ignoring_case("int", "integer"));
    EXPECT_EQ(ascii_lower('!'), '!');
}

TEST(TextTest, FindsBoundariesForEveryUtf8Width) {
    constexpr std::string_view text{"A\xc2\xa2\xe2\x82\xac\xf0\x90\x8d\x88", 10U};

    EXPECT_EQ(utf8_code_point_end(text, 0U), 1U);
    EXPECT_EQ(utf8_code_point_end(text, 1U), 3U);
    EXPECT_EQ(utf8_code_point_end(text, 3U), 6U);
    EXPECT_EQ(utf8_code_point_end(text, 6U), 10U);
    EXPECT_FALSE(utf8_code_point_end(text, text.size()).has_value());
    EXPECT_TRUE(is_valid_utf8(text));
    EXPECT_EQ(valid_utf8_prefix_size(text, 5U), 3U);
    EXPECT_EQ(valid_utf8_prefix_size(text, 9U), 6U);
    EXPECT_EQ(valid_utf8_prefix_size(text, text.size()), text.size());
}

TEST(TextTest, RejectsNoncanonicalSurrogateOutOfRangeAndTruncatedUtf8) {
    constexpr std::string_view overlong{"\xc0\xaf", 2U};
    constexpr std::string_view surrogate{"\xed\xa0\x80", 3U};
    constexpr std::string_view out_of_range{"\xf4\x90\x80\x80", 4U};
    constexpr std::string_view continuation_lead{"\x80", 1U};
    constexpr std::string_view truncated{"\xe2\x82", 2U};

    EXPECT_FALSE(is_valid_utf8(overlong));
    EXPECT_FALSE(is_valid_utf8(surrogate));
    EXPECT_FALSE(is_valid_utf8(out_of_range));
    EXPECT_FALSE(is_valid_utf8(continuation_lead));
    EXPECT_FALSE(is_valid_utf8(truncated));
    EXPECT_FALSE(valid_utf8_prefix_size(truncated, 1U).has_value());
}

}  // namespace
}  // namespace db::base
