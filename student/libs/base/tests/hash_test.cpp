#include <cstddef>

#include <gtest/gtest.h>

#include "db/base/hash.hpp"

namespace db::base {
namespace {

TEST(HashTest, UsesTheSeedAndAlreadyHashedValueInOrder) {
    constexpr std::size_t first = 7U;
    constexpr std::size_t second = 11U;

    EXPECT_NE(combine_hash(first, second), combine_hash(second, first));
    EXPECT_NE(combine_hash(0U, second), combine_hash(first, second));
}

}  // namespace
}  // namespace db::base
