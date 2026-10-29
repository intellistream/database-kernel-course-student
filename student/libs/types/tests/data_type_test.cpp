#include <cstdint>
#include <functional>
#include <type_traits>
#include <unordered_set>

#include <gtest/gtest.h>

#include "db/types/data_type.hpp"

namespace db::types {
namespace {

TEST(DataTypeTest, PreservesPermanentBuiltinIdentifiers) {
    EXPECT_EQ(kUnknownTypeId.value(), 0U);
    EXPECT_EQ(kBoolTypeId.value(), 1U);
    EXPECT_EQ(kInt64TypeId.value(), 2U);
    EXPECT_EQ(kReservedFloat64TypeId.value(), 3U);
    EXPECT_EQ(kCharTypeId.value(), 4U);
    EXPECT_EQ(kTimestampTypeId.value(), 5U);
    EXPECT_EQ(kFloat32TypeId.value(), 6U);
}

TEST(DataTypeTest, KeepsModifierSeparateFromBaseIdentity) {
    static_assert(!std::is_convertible_v<std::int32_t, TypeModifier>);

    constexpr DataType plain_char{kCharTypeId};
    constexpr DataType char_32{kCharTypeId, TypeModifier{32}};

    EXPECT_EQ(plain_char.id(), char_32.id());
    EXPECT_TRUE(plain_char.modifier().is_none());
    EXPECT_EQ(char_32.modifier().value(), 32);
    EXPECT_NE(plain_char, char_32);
}

TEST(DataTypeTest, SupportsOrderingAndHashContainers) {
    std::unordered_set<DataType> types;
    EXPECT_TRUE(types.insert(DataType{kInt64TypeId}).second);
    EXPECT_TRUE(types.insert(DataType{kCharTypeId, TypeModifier{8}}).second);

    EXPECT_LT(DataType{kBoolTypeId}, DataType{kInt64TypeId});
    EXPECT_FALSE(types.insert(DataType{kInt64TypeId}).second);
    EXPECT_FALSE(types.insert(DataType{kCharTypeId, TypeModifier{8}}).second);
    EXPECT_TRUE(types.insert(DataType{kCharTypeId, TypeModifier{16}}).second);
    EXPECT_EQ(types.size(), 3U);
}

}  // namespace
}  // namespace db::types
