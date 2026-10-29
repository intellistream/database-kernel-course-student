#include <cstdint>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/types/type_io.hpp"
#include "db/types/type_registry.hpp"

namespace db::types {
namespace {

TEST(TypeIoTest, ValidatesBuiltinModifiersAndStorageWidths) {
    EXPECT_TRUE(validate_data_type(DataType{kCharTypeId, TypeModifier{1}}));
    EXPECT_TRUE(validate_data_type(DataType{kCharTypeId, TypeModifier{4'048}}));
    EXPECT_FALSE(validate_data_type(DataType{kCharTypeId, TypeModifier{0}}));
    EXPECT_FALSE(validate_data_type(DataType{kCharTypeId, TypeModifier{4'049}}));
    EXPECT_TRUE(validate_data_type(DataType{kTimestampTypeId, TypeModifier{0}}));
    EXPECT_TRUE(validate_data_type(DataType{kTimestampTypeId, TypeModifier{6}}));
    EXPECT_FALSE(validate_data_type(DataType{kTimestampTypeId, TypeModifier{7}}));
    EXPECT_FALSE(validate_data_type(DataType{kInt64TypeId, TypeModifier{0}}));
    EXPECT_FALSE(validate_data_type(DataType{TypeId{999}}));

    const auto integer_width = storage_width(kInt64Type);
    const auto character_width = storage_width(DataType{kCharTypeId, TypeModifier{32}});
    ASSERT_TRUE(integer_width);
    EXPECT_EQ(integer_width.value(), 8U);
    ASSERT_TRUE(character_width);
    EXPECT_EQ(character_width.value(), 32U);
    EXPECT_FALSE(storage_width(kCharType));
    EXPECT_FALSE(is_storable(kUnknownType));
    EXPECT_TRUE(is_storable(kFloat32Type));
}

TEST(TypeIoTest, ParsesAndOutputsScalarBuiltinValues) {
    const auto integer = input_value(kInt64Type, " +42 ");
    const auto minimum = input_value(kInt64Type, "-9223372036854775808");
    const auto floating = input_value(kFloat32Type, "1.25");
    const auto boolean_true = input_value(kBoolType, " TRUE ");
    const auto boolean_false = input_value(kBoolType, "0");

    ASSERT_TRUE(integer);
    EXPECT_EQ(*integer.value().as_integer(), 42);
    ASSERT_TRUE(minimum);
    EXPECT_EQ(*minimum.value().as_integer(), std::numeric_limits<std::int64_t>::min());
    ASSERT_TRUE(floating);
    EXPECT_FLOAT_EQ(*floating.value().as_float(), 1.25F);
    ASSERT_TRUE(boolean_true);
    EXPECT_TRUE(*boolean_true.value().as_boolean());
    ASSERT_TRUE(boolean_false);
    EXPECT_FALSE(*boolean_false.value().as_boolean());
    EXPECT_EQ(output_value(integer.value()).value(), "42");
    EXPECT_EQ(output_value(floating.value()).value(), "1.25");
    EXPECT_EQ(output_value(boolean_true.value()).value(), "true");
}

TEST(TypeIoTest, RejectsInvalidAndOutOfRangeScalarText) {
    const auto integer_overflow = input_value(kInt64Type, "9223372036854775808");
    const auto malformed_integer = input_value(kInt64Type, "+-1");
    const auto float_overflow = input_value(kFloat32Type, "1e1000");
    const auto malformed_float = input_value(kFloat32Type, "1.0x");
    const auto malformed_bool = input_value(kBoolType, "yes");

    EXPECT_FALSE(integer_overflow);
    EXPECT_FALSE(malformed_integer);
    EXPECT_FALSE(float_overflow);
    EXPECT_FALSE(malformed_float);
    EXPECT_FALSE(malformed_bool);
}

TEST(TypeIoTest, PreparedTypeInputBindsParsersAndTypedNulls) {
    const auto boolean = PreparedTypeInput::create(kBoolType);
    const auto integer = PreparedTypeInput::create(kInt64Type);
    const auto floating = PreparedTypeInput::create(kFloat32Type);
    const auto character = PreparedTypeInput::create(DataType{kCharTypeId, TypeModifier{4}});
    const auto timestamp = PreparedTypeInput::create(DataType{kTimestampTypeId, TypeModifier{3}});

    ASSERT_TRUE(boolean);
    ASSERT_TRUE(integer);
    ASSERT_TRUE(floating);
    ASSERT_TRUE(character);
    ASSERT_TRUE(timestamp);
    EXPECT_EQ(boolean->parse("true").value(), Value::boolean(true));
    EXPECT_EQ(integer->parse("42").value(), Value::integer(42));
    EXPECT_FLOAT_EQ(*floating->parse("1.25").value().as_float(), 1.25F);
    EXPECT_EQ(*character->parse("你ab").value().as_string(), "你a");
    EXPECT_EQ(
        timestamp->parse("2000-01-01 00:00:00.1236").value().as_timestamp()->microseconds(),
        124'000
    );
    EXPECT_EQ(boolean->null_value(), Value::null(kBoolType));
    EXPECT_EQ(character->null_value(), Value::null(DataType{kCharTypeId, TypeModifier{4}}));

    const auto invalid_modifier = PreparedTypeInput::create(DataType{kCharTypeId, TypeModifier{0}});
    ASSERT_FALSE(invalid_modifier);
    EXPECT_EQ(invalid_modifier.error().code(), base::ErrorCode::kInvalidArgument);

    const auto unknown = PreparedTypeInput::create(kUnknownType);
    ASSERT_TRUE(unknown);
    const auto unknown_value = unknown->parse("literal");
    ASSERT_FALSE(unknown_value);
    EXPECT_EQ(unknown_value.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(TypeIoTest, PreparedTypeInputReusesCompatibleCharacterStorage) {
    const DataType character_type{kCharTypeId, TypeModifier{64}};
    const auto character = PreparedTypeInput::create(character_type);
    ASSERT_TRUE(character);
    auto destination = Value::character(std::string(64U, 'x'), TypeModifier{64});
    ASSERT_NE(destination.as_string(), nullptr);
    const auto* const allocation = destination.as_string()->data();
    const auto capacity = destination.as_string()->capacity();

    ASSERT_TRUE(character->parse_into("reused", destination));
    ASSERT_NE(destination.as_string(), nullptr);
    EXPECT_EQ(*destination.as_string(), "reused");
    EXPECT_EQ(destination.data_type(), character_type);
    EXPECT_EQ(destination.as_string()->data(), allocation);
    EXPECT_EQ(destination.as_string()->capacity(), capacity);

    const std::string invalid_utf8{"\xc0\xaf", 2};
    const auto rejected = character->parse_into(invalid_utf8, destination);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(*destination.as_string(), "reused");
}

TEST(TypeIoTest, TruncatesCharOnUtf8BoundaryAndRejectsInvalidUtf8) {
    const auto truncated = input_value(DataType{kCharTypeId, TypeModifier{4}}, "你ab");
    const std::string invalid_utf8{"\xc0\xaf", 2};
    const auto invalid = input_value(kCharType, invalid_utf8);

    ASSERT_TRUE(truncated);
    EXPECT_EQ(*truncated.value().as_string(), "你a");
    EXPECT_EQ(truncated.value().data_type(), DataType(kCharTypeId, TypeModifier{4}));
    EXPECT_EQ(output_value(truncated.value()).value(), "你a");
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(TypeIoTest, ParsesRoundsAndOutputsTimestamp) {
    const auto value =
        input_value(DataType{kTimestampTypeId, TypeModifier{3}}, "2000-01-01 00:00:00.1236");

    ASSERT_TRUE(value);
    EXPECT_EQ(value.value().as_timestamp()->microseconds(), 124'000);
    EXPECT_EQ(value.value().data_type(), DataType(kTimestampTypeId, TypeModifier{3}));
    const auto output = output_value(value.value());
    ASSERT_TRUE(output);
    EXPECT_EQ(output.value(), "2000-01-01 00:00:00.124");
}

TEST(TypeIoTest, AppliesModifiersToValuesAndTypedNulls) {
    const auto character =
        apply_type_modifier(Value::character("你ab"), DataType{kCharTypeId, TypeModifier{4}});
    const auto timestamp = apply_type_modifier(
        Value::timestamp(TimestampValue{1'550'000}),
        DataType{kTimestampTypeId, TypeModifier{1}}
    );
    const auto null_value =
        apply_type_modifier(Value::null(kUnknownType), DataType{kCharTypeId, TypeModifier{8}});

    ASSERT_TRUE(character);
    EXPECT_EQ(*character.value().as_string(), "你a");
    ASSERT_TRUE(timestamp);
    EXPECT_EQ(timestamp.value().as_timestamp()->microseconds(), 1'600'000);
    ASSERT_TRUE(null_value);
    EXPECT_TRUE(null_value.value().is_null());
    EXPECT_EQ(null_value.value().data_type(), DataType(kCharTypeId, TypeModifier{8}));
}

TEST(TypeIoTest, RejectsUnknownInputNullOutputAndMismatchedModifierApplication) {
    const auto unknown = input_value(kUnknownType, "literal");
    const auto null_output = output_value(Value::null(kInt64Type));
    const auto mismatch =
        apply_type_modifier(Value::integer(1), DataType{kCharTypeId, TypeModifier{8}});

    EXPECT_FALSE(unknown);
    EXPECT_FALSE(null_output);
    EXPECT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error().code(), base::ErrorCode::kInvalidArgument);
}

}  // namespace
}  // namespace db::types
