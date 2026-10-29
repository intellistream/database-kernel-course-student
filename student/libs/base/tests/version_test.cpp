#include <gtest/gtest.h>

#include "db/base/version.hpp"

namespace db::base {

TEST(VersionTest, ExposesProjectVersion) {
    constexpr Version expected{.major = 0, .minor = 1, .patch = 0};

    EXPECT_EQ(kVersion, expected);
    EXPECT_EQ(version_string(), "0.1.0");
}

}  // namespace db::base
