#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/types/coercion.hpp"

namespace db::types {
namespace {

TEST(CoercionTest, ResolvesTheDocumentedBuiltinGraphThroughThePublicBoundary) {
    const auto int_to_float =
        find_coercion_path(kInt64Type, kFloat32Type, CoercionContext::kImplicit);
    const auto float_to_int =
        find_coercion_path(kFloat32Type, kInt64Type, CoercionContext::kAssignment);
    const auto int_to_bool = find_coercion_path(kInt64Type, kBoolType, CoercionContext::kExplicit);
    const auto bool_to_int = find_coercion_path(kBoolType, kInt64Type, CoercionContext::kExplicit);

    ASSERT_TRUE(int_to_float);
    ASSERT_TRUE(float_to_int);
    ASSERT_TRUE(int_to_bool);
    ASSERT_TRUE(bool_to_int);
    EXPECT_NE(*int_to_float, *float_to_int);
    EXPECT_NE(*int_to_bool, *bool_to_int);
}

TEST(CoercionTest, EnablesRulesOnlyAtOrAboveTheirContext) {
    EXPECT_TRUE(find_coercion_path(kInt64Type, kFloat32Type, CoercionContext::kImplicit));
    EXPECT_FALSE(find_coercion_path(kFloat32Type, kInt64Type, CoercionContext::kImplicit));
    EXPECT_TRUE(find_coercion_path(kFloat32Type, kInt64Type, CoercionContext::kAssignment));
    EXPECT_FALSE(find_coercion_path(kInt64Type, kBoolType, CoercionContext::kAssignment));
    EXPECT_TRUE(find_coercion_path(kInt64Type, kBoolType, CoercionContext::kExplicit));
}

TEST(CoercionTest, ResolvesRelabelAndExplicitViaIoSeparately) {
    const auto relabel = find_coercion_path(
        kCharType,
        DataType{kCharTypeId, TypeModifier{4}},
        CoercionContext::kImplicit
    );
    const auto via_io = find_coercion_path(
        kInt64Type,
        DataType{kCharTypeId, TypeModifier{8}},
        CoercionContext::kExplicit
    );
    const auto forbidden = find_coercion_path(kInt64Type, kCharType, CoercionContext::kAssignment);

    ASSERT_TRUE(relabel);
    EXPECT_TRUE(relabel->is_relabel());
    ASSERT_TRUE(via_io);
    EXPECT_FALSE(via_io->is_relabel());
    EXPECT_FALSE(forbidden);
}

TEST(CoercionTest, AppliesImplicitIntegerToBinary32) {
    const auto path = find_coercion_path(kInt64Type, kFloat32Type, CoercionContext::kImplicit);
    ASSERT_TRUE(path);
    const auto converted = apply_coercion(Value::integer(16'777'217), kFloat32Type, *path);

    ASSERT_TRUE(converted);
    ASSERT_NE(converted.value().as_float(), nullptr);
    EXPECT_EQ(*converted.value().as_float(), 16'777'216.0F);
    EXPECT_EQ(converted.value().data_type(), kFloat32Type);
}

TEST(CoercionTest, RoundsFloatToIntegerWithTiesToEven) {
    const auto path = find_coercion_path(kFloat32Type, kInt64Type, CoercionContext::kAssignment);
    ASSERT_TRUE(path);

    const auto positive_half = apply_coercion(Value::floating(1.5F), kInt64Type, *path);
    const auto even_half = apply_coercion(Value::floating(2.5F), kInt64Type, *path);
    const auto negative_half = apply_coercion(Value::floating(-1.5F), kInt64Type, *path);
    const auto ordinary = apply_coercion(Value::floating(1.9F), kInt64Type, *path);

    ASSERT_TRUE(positive_half);
    EXPECT_EQ(*positive_half.value().as_integer(), 2);
    ASSERT_TRUE(even_half);
    EXPECT_EQ(*even_half.value().as_integer(), 2);
    ASSERT_TRUE(negative_half);
    EXPECT_EQ(*negative_half.value().as_integer(), -2);
    ASSERT_TRUE(ordinary);
    EXPECT_EQ(*ordinary.value().as_integer(), 2);
}

TEST(CoercionTest, RejectsNonFiniteAndOutOfRangeFloatToInteger) {
    const auto path = find_coercion_path(kFloat32Type, kInt64Type, CoercionContext::kExplicit);
    ASSERT_TRUE(path);

    const auto nan =
        apply_coercion(Value::floating(std::numeric_limits<float>::quiet_NaN()), kInt64Type, *path);
    const auto infinity =
        apply_coercion(Value::floating(std::numeric_limits<float>::infinity()), kInt64Type, *path);
    const auto bound =
        apply_coercion(Value::floating(9'223'372'036'854'775'808.0F), kInt64Type, *path);

    EXPECT_FALSE(nan);
    EXPECT_FALSE(infinity);
    ASSERT_FALSE(bound);
    EXPECT_EQ(bound.error().code(), base::ErrorCode::kOutOfRange);
}

TEST(CoercionTest, AppliesExplicitBooleanCastsAndViaIo) {
    const auto int_to_bool = find_coercion_path(kInt64Type, kBoolType, CoercionContext::kExplicit);
    const auto bool_to_int = find_coercion_path(kBoolType, kInt64Type, CoercionContext::kExplicit);
    const auto int_to_char = find_coercion_path(
        kInt64Type,
        DataType{kCharTypeId, TypeModifier{4}},
        CoercionContext::kExplicit
    );
    ASSERT_TRUE(int_to_bool);
    ASSERT_TRUE(bool_to_int);
    ASSERT_TRUE(int_to_char);

    const auto boolean = apply_coercion(Value::integer(-1), kBoolType, *int_to_bool);
    const auto integer = apply_coercion(Value::boolean(true), kInt64Type, *bool_to_int);
    const auto character =
        apply_coercion(Value::integer(12345), DataType{kCharTypeId, TypeModifier{4}}, *int_to_char);
    ASSERT_TRUE(boolean);
    EXPECT_TRUE(*boolean.value().as_boolean());
    ASSERT_TRUE(integer);
    EXPECT_EQ(*integer.value().as_integer(), 1);
    ASSERT_TRUE(character);
    EXPECT_EQ(*character.value().as_string(), "1234");
}

TEST(CoercionTest, RetagsNullAndRejectsAResolvedPathUsedWithTheWrongInput) {
    const auto relabel = find_coercion_path(
        kCharType,
        DataType{kCharTypeId, TypeModifier{8}},
        CoercionContext::kImplicit
    );
    const auto bool_to_int = find_coercion_path(kBoolType, kInt64Type, CoercionContext::kExplicit);
    ASSERT_TRUE(relabel);
    ASSERT_TRUE(bool_to_int);
    const auto null_value =
        apply_coercion(Value::null(kUnknownType), DataType{kCharTypeId, TypeModifier{8}}, *relabel);
    const auto mismatched = apply_coercion(Value::integer(1), kInt64Type, *bool_to_int);

    ASSERT_TRUE(null_value);
    EXPECT_TRUE(null_value.value().is_null());
    EXPECT_EQ(null_value.value().data_type(), DataType(kCharTypeId, TypeModifier{8}));
    EXPECT_FALSE(mismatched);
}

}  // namespace
}  // namespace db::types
