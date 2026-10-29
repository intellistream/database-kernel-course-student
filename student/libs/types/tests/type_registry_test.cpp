#include <array>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "db/types/type_registry.hpp"

namespace db::types {
namespace {

TEST(TypeRegistryTest, ResolvesEveryBuiltinThroughStableIdentityQueries) {
    constexpr std::array builtins{
        std::pair{kUnknownTypeId, std::string_view{"unknown"}},
        std::pair{kBoolTypeId, std::string_view{"bool"}},
        std::pair{kInt64TypeId, std::string_view{"int"}},
        std::pair{kFloat32TypeId, std::string_view{"float"}},
        std::pair{kCharTypeId, std::string_view{"char"}},
        std::pair{kTimestampTypeId, std::string_view{"timestamp"}},
    };

    for (const auto& [id, name] : builtins) {
        EXPECT_TRUE(is_known_type(id));
        EXPECT_EQ(type_name(id), name);
        EXPECT_EQ(find_type_id(name), id);
    }
}

TEST(TypeRegistryTest, FindsCanonicalNamesAndAliasesCaseInsensitively) {
    EXPECT_EQ(find_type_id("BOOL"), kBoolTypeId);
    EXPECT_EQ(find_type_id("boolean"), kBoolTypeId);
    EXPECT_EQ(find_type_id("BIGINT"), kInt64TypeId);
    EXPECT_EQ(find_type_id("Double Precision"), kFloat32TypeId);
    EXPECT_EQ(find_type_id("CHARACTER"), kCharTypeId);
    EXPECT_EQ(find_type_id("Timestamp Without Time Zone"), kTimestampTypeId);
}

TEST(TypeRegistryTest, ExposesStoragePolicyOnlyThroughCheckedBehavior) {
    EXPECT_EQ(storage_width(kBoolType).value(), 1U);
    EXPECT_EQ(storage_width(kInt64Type).value(), 8U);
    EXPECT_EQ(storage_width(kFloat32Type).value(), 4U);
    EXPECT_EQ(storage_width(kTimestampType).value(), 8U);
    EXPECT_EQ(storage_width(DataType{kCharTypeId, TypeModifier{12}}).value(), 12U);
    EXPECT_FALSE(storage_width(kUnknownType));
    EXPECT_FALSE(storage_width(kCharType));
}

TEST(TypeRegistryTest, RejectsUnknownAndReservedIdentities) {
    EXPECT_FALSE(is_known_type(kReservedFloat64TypeId));
    EXPECT_FALSE(is_known_type(TypeId{999}));
    EXPECT_FALSE(type_name(kReservedFloat64TypeId).has_value());
    EXPECT_FALSE(type_name(TypeId{999}).has_value());
    EXPECT_FALSE(find_type_id("int4").has_value());
    EXPECT_FALSE(find_type_id("").has_value());
}

}  // namespace
}  // namespace db::types
