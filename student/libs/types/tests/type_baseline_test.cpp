#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "db/types/codec.hpp"
#include "db/types/coercion.hpp"
#include "db/types/datetime.hpp"
#include "db/types/type_io.hpp"
#include "db/types/value.hpp"

namespace db::types {
namespace {

auto bytes(std::initializer_list<unsigned int> values) -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.reserve(values.size());
    for (const auto value : values) {
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

auto coerce(const Value& value, const DataType target, const CoercionContext context) -> Value {
    const auto path = find_coercion_path(value.data_type(), target, context);
    EXPECT_TRUE(path.has_value());
    if (!path.has_value()) {
        return Value::null(target);
    }
    auto result = apply_coercion(value, target, *path);
    EXPECT_TRUE(result);
    if (!result) {
        return Value::null(target);
    }
    return std::move(result).value();
}

TEST(TypeBaselineTest, ValueKindOrderMatchesReferenceScenario) {
    const auto null_value = Value::null(kUnknownType);
    const auto boolean = Value::boolean(true);
    const auto integer = Value::integer(1);
    const auto floating = Value::floating(1.0F);
    const auto string = Value::character("a");
    const auto timestamp = Value::timestamp(TimestampValue{0});

    EXPECT_EQ(value_kind_order(null_value, boolean), std::strong_ordering::less);
    EXPECT_EQ(value_kind_order(boolean, integer), std::strong_ordering::less);
    EXPECT_EQ(value_kind_order(integer, floating), std::strong_ordering::less);
    EXPECT_EQ(value_kind_order(floating, string), std::strong_ordering::less);
    EXPECT_EQ(value_kind_order(string, timestamp), std::strong_ordering::less);
    EXPECT_NE(value_kind_order(integer, string), std::strong_ordering::equal);
}

TEST(TypeBaselineTest, BuiltinInputAndTimestampScenariosMatchReference) {
    const auto integer = input_value(kInt64Type, " 42 ");
    const auto floating = input_value(kFloat32Type, "1.25");
    const auto boolean = input_value(kBoolType, "TRUE");
    const auto character = input_value(DataType{kCharTypeId, TypeModifier{4}}, "你ab");
    const auto timestamp =
        input_value(DataType{kTimestampTypeId, TypeModifier{3}}, "2000-01-01 00:00:00.1236");

    ASSERT_TRUE(integer);
    EXPECT_EQ(integer.value(), Value::integer(42));
    ASSERT_TRUE(floating);
    EXPECT_EQ(floating.value(), Value::floating(1.25F));
    ASSERT_TRUE(boolean);
    EXPECT_EQ(boolean.value(), Value::boolean(true));
    ASSERT_TRUE(character);
    EXPECT_EQ(character.value(), Value::character("你a", TypeModifier{4}));
    ASSERT_TRUE(timestamp);
    EXPECT_EQ(timestamp.value(), Value::timestamp(TimestampValue{124'000}, TypeModifier{3}));
    EXPECT_EQ(output_value(timestamp.value()).value(), "2000-01-01 00:00:00.124");
}

TEST(TypeBaselineTest, CastMatrixFoundationalSemanticsMatchReference) {
    EXPECT_EQ(
        coerce(Value::integer(7), kFloat32Type, CoercionContext::kImplicit),
        Value::floating(7.0F)
    );
    EXPECT_EQ(
        coerce(Value::floating(8.6F), kInt64Type, CoercionContext::kAssignment),
        Value::integer(9)
    );
    EXPECT_EQ(
        coerce(Value::character("123", TypeModifier{8}), kInt64Type, CoercionContext::kExplicit),
        Value::integer(123)
    );
    EXPECT_EQ(
        coerce(Value::boolean(true), kInt64Type, CoercionContext::kExplicit),
        Value::integer(1)
    );
    EXPECT_EQ(
        coerce(
            Value::integer(9),
            DataType{kCharTypeId, TypeModifier{8}},
            CoercionContext::kExplicit
        ),
        Value::character("9", TypeModifier{8})
    );
    EXPECT_FALSE(find_coercion_path(kInt64Type, kBoolType, CoercionContext::kImplicit));
}

TEST(TypeBaselineTest, TimestampMatrixRoundingOrderingAndRenderingMatchReference) {
    const DataType type{kTimestampTypeId, TypeModifier{3}};
    const auto before = input_value(type, "1999-12-31 23:59:59.9994");
    const auto epoch = input_value(type, "2000-01-01 00:00:00");
    const auto rounded = input_value(type, "2000-01-01 00:00:00.1236");
    const auto next_day = input_value(type, "2000-01-02 00:00:00");
    const auto invalid = input_value(kTimestampType, "2023-02-29 00:00:00");

    ASSERT_TRUE(before);
    ASSERT_TRUE(epoch);
    ASSERT_TRUE(rounded);
    ASSERT_TRUE(next_day);
    EXPECT_EQ(before.value().as_timestamp()->microseconds(), -1'000);
    EXPECT_EQ(epoch.value().as_timestamp()->microseconds(), 0);
    EXPECT_EQ(rounded.value().as_timestamp()->microseconds(), 124'000);
    EXPECT_EQ(next_day.value().as_timestamp()->microseconds(), 86'400'000'000);
    EXPECT_EQ(output_value(rounded.value()).value(), "2000-01-01 00:00:00.124");
    EXPECT_FALSE(invalid);

    const auto epoch_key = key_encode(epoch.value());
    const auto rounded_key = key_encode(rounded.value());
    const auto next_day_key = key_encode(next_day.value());
    ASSERT_TRUE(epoch_key);
    ASSERT_TRUE(rounded_key);
    ASSERT_TRUE(next_day_key);
    EXPECT_EQ(
        key_compare(epoch_key.value(), rounded_key.value(), type).value(),
        std::strong_ordering::less
    );
    EXPECT_EQ(
        key_compare(rounded_key.value(), next_day_key.value(), type).value(),
        std::strong_ordering::less
    );
}

TEST(TypeBaselineTest, FloatRoundingAndOrderedKeyBoundaryMatrixIsStable) {
    const std::vector<std::pair<float, std::int64_t>> casts{
        {-2.5F, -2},
        {-1.5F, -2},
        {-0.5F, 0},
        {0.5F, 0},
        {1.5F, 2},
        {2.5F, 2},
    };
    for (const auto& [input, expected] : casts) {
        EXPECT_EQ(
            coerce(Value::floating(input), kInt64Type, CoercionContext::kAssignment),
            Value::integer(expected)
        );
    }

    const std::vector<float> ordered{
        -std::numeric_limits<float>::infinity(),
        -1.0F,
        -0.0F,
        0.0F,
        1.0F,
        std::numeric_limits<float>::infinity(),
        std::bit_cast<float>(std::uint32_t{0x7fc0'1234U}),
    };
    auto previous = key_encode(Value::floating(ordered.front())).value();
    for (std::size_t index = 1; index < ordered.size(); ++index) {
        auto current = key_encode(Value::floating(ordered[index])).value();
        EXPECT_EQ(key_compare(previous, current, kFloat32Type).value(), std::strong_ordering::less);
        previous = std::move(current);
    }
}

TEST(CodecBaselineTest, VersionOneHeapAndKeyFixturesRemainStable) {
    struct Fixture final {
        Value value;
        DataType heap_type;
        std::vector<std::byte> heap;
        std::vector<std::byte> key;
    };

    const std::vector<Fixture> fixtures{
        {Value::boolean(true), kBoolType, bytes({0x01}), bytes({0x01})},
        {Value::integer(-2),
         kInt64Type,
         bytes({0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}),
         bytes({0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe})},
        {Value::floating(-1.5F),
         kFloat32Type,
         bytes({0x00, 0x00, 0xc0, 0xbf}),
         bytes({0x40, 0x3f, 0xff, 0xff})},
        {Value::character("你a", TypeModifier{6}),
         DataType{kCharTypeId, TypeModifier{6}},
         bytes({0xe4, 0xbd, 0xa0, 0x61, 0x00, 0x00}),
         bytes({0x00, 0x04, 0xe4, 0xbd, 0xa0, 0x61})},
        {Value::timestamp(TimestampValue{123'456}),
         kTimestampType,
         bytes({0x40, 0xe2, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}),
         bytes({0x80, 0x00, 0x00, 0x00, 0x00, 0x01, 0xe2, 0x40})},
    };

    for (const auto& fixture : fixtures) {
        const auto heap = heap_encode(fixture.value, fixture.heap_type);
        const auto key = key_encode(fixture.value);
        ASSERT_TRUE(heap);
        ASSERT_TRUE(key);
        EXPECT_EQ(heap.value(), fixture.heap);
        EXPECT_EQ(key.value(), fixture.key);

        const auto decoded = heap_decode(fixture.heap, fixture.heap_type);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded.value().value, fixture.value);
        EXPECT_EQ(decoded.value().bytes_consumed, fixture.heap.size());
    }
}

}  // namespace
}  // namespace db::types
