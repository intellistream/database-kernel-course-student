#include <array>
#include <bit>
#include <compare>
#include <cstdint>
#include <string>
#include <unordered_set>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/types/value.hpp"

namespace db::types {
namespace {

TEST(ValueTest, CarriesTypedNullWithoutInventingPayload) {
    const auto null_int = Value::null(DataType{kInt64TypeId});
    const auto null_char = Value::null(DataType{kCharTypeId, TypeModifier{8}});

    EXPECT_TRUE(null_int.is_null());
    EXPECT_EQ(null_int.kind(), ValueKind::kNull);
    EXPECT_EQ(null_int.data_type(), DataType{kInt64TypeId});
    EXPECT_EQ(null_int.display(), "NULL");
    EXPECT_NE(null_int, null_char);
    EXPECT_NE(null_int.hash(), null_char.hash());
}

TEST(ValueTest, ExposesPayloadsWithTheirExactDataTypes) {
    const auto boolean = Value::boolean(true);
    const auto integer = Value::integer(-42);
    const auto floating = Value::floating(1.25F);
    const auto character = Value::character("abc", TypeModifier{8});
    const auto unknown = Value::unknown_string("literal");
    const auto timestamp = Value::timestamp(TimestampValue{123'456}, TypeModifier{3});

    ASSERT_NE(boolean.as_boolean(), nullptr);
    EXPECT_TRUE(*boolean.as_boolean());
    EXPECT_EQ(*integer.as_integer(), -42);
    EXPECT_FLOAT_EQ(*floating.as_float(), 1.25F);
    EXPECT_EQ(*character.as_string(), "abc");
    EXPECT_EQ(character.data_type(), DataType(kCharTypeId, TypeModifier{8}));
    EXPECT_EQ(unknown.data_type(), DataType{kUnknownTypeId});
    EXPECT_EQ(timestamp.as_timestamp()->microseconds(), 123'456);
    EXPECT_EQ(timestamp.data_type(), DataType(kTimestampTypeId, TypeModifier{3}));
    EXPECT_EQ(integer.as_string(), nullptr);
}

TEST(ValueTest, UsesRawBinary32BitsForStructuralEqualityAndHashing) {
    const auto positive_zero = Value::floating(0.0F);
    const auto negative_zero = Value::floating(-0.0F);
    const auto first_nan = Value::floating(std::bit_cast<float>(std::uint32_t{0x7fc00001U}));
    const auto same_nan = Value::floating(std::bit_cast<float>(std::uint32_t{0x7fc00001U}));
    const auto other_nan = Value::floating(std::bit_cast<float>(std::uint32_t{0x7fc00002U}));

    EXPECT_NE(positive_zero, negative_zero);
    EXPECT_NE(positive_zero.hash(), negative_zero.hash());
    EXPECT_EQ(first_nan, same_nan);
    EXPECT_EQ(first_nan.hash(), same_nan.hash());
    EXPECT_NE(first_nan, other_nan);
}

TEST(ValueTest, ProvidesStableDiagnosticDisplay) {
    EXPECT_EQ(Value::boolean(false).display(), "false");
    EXPECT_EQ(Value::integer(-17).display(), "-17");
    EXPECT_EQ(Value::floating(1.5F).display(), "1.5");
    EXPECT_EQ(Value::character("it's").display(), "'it''s'");
    EXPECT_EQ(Value::timestamp(TimestampValue{-9}).display(), "1999-12-31 23:59:59.999991");
}

TEST(ValueTest, HashesStructuralValuesForStandardContainers) {
    const std::unordered_set<Value> values{
        Value::integer(7),
        Value::character("seven"),
        Value::null(DataType{kBoolTypeId}),
    };

    EXPECT_TRUE(values.contains(Value::integer(7)));
    EXPECT_TRUE(values.contains(Value::character("seven")));
    EXPECT_TRUE(values.contains(Value::null(DataType{kBoolTypeId})));
    EXPECT_FALSE(values.contains(Value::integer(8)));
}

TEST(ValueTest, ComparesConcreteValuesAndPropagatesSqlNull) {
    const auto less = sql_compare(Value::integer(3), Value::integer(5));
    const auto equal = sql_equal(Value::character("same"), Value::character("same"));
    const auto unequal = sql_equal(Value::boolean(false), Value::boolean(true));
    const auto null_equal = sql_equal(Value::null(DataType{kInt64TypeId}), Value::integer(1));

    ASSERT_TRUE(less);
    EXPECT_EQ(less.value(), std::partial_ordering::less);
    ASSERT_TRUE(equal);
    EXPECT_EQ(equal.value(), SqlTruth::kTrue);
    ASSERT_TRUE(unequal);
    EXPECT_EQ(unequal.value(), SqlTruth::kFalse);
    ASSERT_TRUE(null_equal);
    EXPECT_EQ(null_equal.value(), SqlTruth::kUnknown);
}

TEST(ValueTest, KeepsNanUnorderedAndRequiresCoercionAcrossTypes) {
    const auto nan = Value::floating(std::bit_cast<float>(std::uint32_t{0x7fc00001U}));
    const auto nan_comparison = sql_compare(nan, Value::floating(1.0F));
    const auto cross_type = sql_compare(Value::integer(1), Value::floating(1.0F));
    const auto unknown = sql_equal(Value::unknown_string("1"), Value::unknown_string("1"));

    ASSERT_TRUE(nan_comparison);
    EXPECT_EQ(nan_comparison.value(), std::partial_ordering::unordered);
    ASSERT_FALSE(cross_type);
    EXPECT_EQ(cross_type.error().code(), base::ErrorCode::kInvalidArgument);
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(ValueTest, OrdersKindsIndependentlyFromSqlComparison) {
    EXPECT_EQ(
        value_kind_order(Value::null(DataType{kBoolTypeId}), Value::boolean(false)),
        std::strong_ordering::less
    );
    EXPECT_EQ(
        value_kind_order(Value::boolean(true), Value::integer(1)),
        std::strong_ordering::less
    );
    EXPECT_EQ(
        value_kind_order(Value::integer(1), Value::floating(1.0F)),
        std::strong_ordering::less
    );
    EXPECT_EQ(
        value_kind_order(Value::floating(1.0F), Value::character("1")),
        std::strong_ordering::less
    );
    EXPECT_EQ(
        value_kind_order(Value::character("1"), Value::timestamp(TimestampValue{1})),
        std::strong_ordering::less
    );
}

TEST(SqlTruthTest, ImplementsThreeValuedBooleanTables) {
    constexpr std::array values{SqlTruth::kFalse, SqlTruth::kTrue, SqlTruth::kUnknown};
    constexpr std::array and_table{
        SqlTruth::kFalse,
        SqlTruth::kFalse,
        SqlTruth::kFalse,
        SqlTruth::kFalse,
        SqlTruth::kTrue,
        SqlTruth::kUnknown,
        SqlTruth::kFalse,
        SqlTruth::kUnknown,
        SqlTruth::kUnknown,
    };
    constexpr std::array or_table{
        SqlTruth::kFalse,
        SqlTruth::kTrue,
        SqlTruth::kUnknown,
        SqlTruth::kTrue,
        SqlTruth::kTrue,
        SqlTruth::kTrue,
        SqlTruth::kUnknown,
        SqlTruth::kTrue,
        SqlTruth::kUnknown,
    };

    for (std::size_t left = 0; left < values.size(); ++left) {
        for (std::size_t right = 0; right < values.size(); ++right) {
            const auto index = (left * values.size()) + right;
            EXPECT_EQ(sql_and(values.at(left), values.at(right)), and_table.at(index));
            EXPECT_EQ(sql_or(values.at(left), values.at(right)), or_table.at(index));
        }
    }
    EXPECT_EQ(sql_not(SqlTruth::kFalse), SqlTruth::kTrue);
    EXPECT_EQ(sql_not(SqlTruth::kTrue), SqlTruth::kFalse);
    EXPECT_EQ(sql_not(SqlTruth::kUnknown), SqlTruth::kUnknown);
}

}  // namespace
}  // namespace db::types
