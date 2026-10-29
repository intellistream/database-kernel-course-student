#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/tuple_codec.hpp"
#include "db/base/error.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::access {
namespace {

auto sample_schema() -> TupleSchema {
    auto schema = TupleSchema::create({
        {.name = "active", .data_type = types::kBoolType},
        {.name = "count", .data_type = types::kInt64Type},
        {.name = "label", .data_type = types::DataType{types::kCharTypeId, types::TypeModifier{5}}},
        {.name = "at", .data_type = types::kTimestampType},
    });
    EXPECT_TRUE(schema) << schema.error().to_string();
    return std::move(schema).value();
}

TEST(TupleSchemaTest, ValidatesNamesTypesAndStablePositions) {
    const auto schema = sample_schema();
    EXPECT_EQ(schema.size(), 4U);
    EXPECT_FALSE(schema.empty());
    EXPECT_EQ(schema.null_bitmap_size(), 1U);
    EXPECT_EQ(schema.encoded_size(), 1U + 1U + 8U + 5U + 8U);
    EXPECT_EQ(schema.position("label").value(), 2U);
    EXPECT_EQ(schema.position("missing").error().code(), base::ErrorCode::kNotFound);

    EXPECT_EQ(
        TupleSchema::create({
                                {.name = "same", .data_type = types::kInt64Type},
                                {.name = "same", .data_type = types::kBoolType},
                            })
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_FALSE(TupleSchema::create({{.name = "", .data_type = types::kInt64Type}}));
    EXPECT_FALSE(TupleSchema::create({{.name = "unknown", .data_type = types::kUnknownType}}));
}

TEST(TupleCodecTest, RoundTripsTypedValuesAndFixedWidthNullSlots) {
    const auto schema = sample_schema();
    const std::vector<types::Value> values{
        types::Value::boolean(true),
        types::Value::null(types::kInt64Type),
        types::Value::character("xy", types::TypeModifier{5}),
        types::Value::timestamp(types::TimestampValue{-123}),
    };

    auto encoded = encode_row(schema, values);
    ASSERT_TRUE(encoded) << encoded.error().to_string();
    ASSERT_EQ(encoded->size(), schema.encoded_size());
    EXPECT_EQ(std::to_integer<std::uint8_t>(encoded->front()), 0x02U);
    for (std::size_t index = 2; index < 10; ++index) {
        EXPECT_EQ((*encoded)[index], std::byte{0});
    }
    auto decoded = decode_row(schema, encoded.value());
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(decoded.value(), values);
}

TEST(TupleCodecTest, ProjectedDecodeRetainsShapeWithTypedNullPlaceholders) {
    const auto schema = sample_schema();
    const std::vector<types::Value> values{
        types::Value::boolean(true),
        types::Value::integer(42),
        types::Value::character("xy", types::TypeModifier{5}),
        types::Value::null(types::kTimestampType),
    };
    const auto encoded = encode_row(schema, values);
    ASSERT_TRUE(encoded) << encoded.error().to_string();
    const std::vector usage{
        TupleColumnUsage::kSkipped,
        TupleColumnUsage::kMaterialized,
        TupleColumnUsage::kSkipped,
        TupleColumnUsage::kSkipped,
    };
    auto decoded = decode_row_projected(schema, encoded.value(), usage);
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(
        decoded.value(),
        (std::vector<types::Value>{
            types::Value::null(types::kBoolType),
            types::Value::integer(42),
            types::Value::null(
                types::DataType{
                    types::kCharTypeId,
                    types::TypeModifier{5},
                }
            ),
            types::Value::null(types::kTimestampType),
        })
    );

    EXPECT_EQ(
        decode_row_projected(schema, encoded.value(), std::span{usage}.first(3U)).error().code(),
        base::ErrorCode::kInvalidArgument
    );

    auto corrupt_null = encoded.value();
    corrupt_null.at(15U) = std::byte{1};
    EXPECT_EQ(
        decode_row_projected(schema, corrupt_null, usage).error().code(),
        base::ErrorCode::kCorruption
    );
}

TEST(TupleCodecTest, EncodeIntoReusesCallerOwnedTupleStorage) {
    const auto schema = sample_schema();
    const std::vector<types::Value> first{
        types::Value::boolean(true),
        types::Value::integer(7),
        types::Value::character("one", types::TypeModifier{5}),
        types::Value::timestamp(types::TimestampValue{10}),
    };
    const std::vector<types::Value> second{
        types::Value::boolean(false),
        types::Value::null(types::kInt64Type),
        types::Value::character("two", types::TypeModifier{5}),
        types::Value::timestamp(types::TimestampValue{11}),
    };
    std::vector<std::byte> reusable;
    reusable.reserve(schema.encoded_size() + 16U);

    auto first_encoded = encode_row_into(schema, first, reusable);
    ASSERT_TRUE(first_encoded) << first_encoded.error().to_string();
    const std::byte* const allocation = reusable.data();
    const auto expected_second = encode_row(schema, second);
    ASSERT_TRUE(expected_second) << expected_second.error().to_string();

    auto second_encoded = encode_row_into(schema, second, reusable);
    ASSERT_TRUE(second_encoded) << second_encoded.error().to_string();
    EXPECT_EQ(reusable.data(), allocation);
    EXPECT_EQ(reusable, expected_second.value());
}

TEST(TupleCodecTest, CanonicalEncodeMatchesCheckedEncoding) {
    const auto schema = sample_schema();
    const std::vector<types::Value> values{
        types::Value::boolean(true),
        types::Value::integer(7),
        types::Value::character("你", types::TypeModifier{5}),
        types::Value::timestamp(types::TimestampValue{10}),
    };
    std::vector<std::byte> checked;
    std::vector<std::byte> canonical;
    ASSERT_TRUE(encode_row_into(schema, values, checked));
    ASSERT_TRUE(encode_canonical_row_into(schema, values, canonical));
    EXPECT_EQ(canonical, checked);
}

TEST(TupleCodecTest, DecodeIntoReusesCallerOwnedValueStorage) {
    const auto schema = sample_schema();
    const std::vector<types::Value> values{
        types::Value::boolean(true),
        types::Value::integer(7),
        types::Value::character("row", types::TypeModifier{5}),
        types::Value::timestamp(types::TimestampValue{10}),
    };
    const auto encoded = encode_row(schema, values);
    ASSERT_TRUE(encoded) << encoded.error().to_string();

    std::vector<types::Value> output;
    output.reserve(schema.size() + 4U);
    const types::Value* const allocation = output.data();
    auto decoded = decode_row_into(schema, encoded.value(), output);
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(output.data(), allocation);
    EXPECT_EQ(output, values);

    const std::vector usage{
        TupleColumnUsage::kSkipped,
        TupleColumnUsage::kMaterialized,
        TupleColumnUsage::kSkipped,
        TupleColumnUsage::kMaterialized,
    };
    decoded = decode_row_projected_into(schema, encoded.value(), usage, output);
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(output.data(), allocation);
    EXPECT_EQ(output[0U], types::Value::null(types::kBoolType));
    EXPECT_EQ(output[1U], types::Value::integer(7));
    EXPECT_EQ(
        output[2U],
        types::Value::null(
            types::DataType{
                types::kCharTypeId,
                types::TypeModifier{5},
            }
        )
    );
    EXPECT_EQ(output[3U], types::Value::timestamp(types::TimestampValue{10}));
}

TEST(TupleCodecTest, RejectsShapeTypeAndCanonicalEncodingViolations) {
    const auto schema = sample_schema();
    const std::vector<types::Value> too_short{types::Value::boolean(true)};
    EXPECT_EQ(encode_row(schema, too_short).error().code(), base::ErrorCode::kInvalidArgument);

    const std::vector<types::Value> wrong_type{
        types::Value::integer(1),
        types::Value::integer(2),
        types::Value::character("xy", types::TypeModifier{5}),
        types::Value::timestamp(types::TimestampValue{0}),
    };
    EXPECT_EQ(encode_row(schema, wrong_type).error().code(), base::ErrorCode::kInvalidArgument);

    const std::vector<types::Value> valid{
        types::Value::boolean(false),
        types::Value::null(types::kInt64Type),
        types::Value::character("ok", types::TypeModifier{5}),
        types::Value::timestamp(types::TimestampValue{0}),
    };
    auto encoded = encode_row(schema, valid).value();
    encoded[2] = std::byte{1};
    EXPECT_EQ(decode_row(schema, encoded).error().code(), base::ErrorCode::kCorruption);
    encoded[2] = std::byte{0};
    encoded[0] |= std::byte{0x80};
    EXPECT_EQ(decode_row(schema, encoded).error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(
        decode_row(schema, base::Bytes{encoded}.first(encoded.size() - 1U)).error().code(),
        base::ErrorCode::kCorruption
    );
}

TEST(TupleCodecTest, EmptySchemaHasCanonicalEmptyRow) {
    auto schema = TupleSchema::create({}).value();
    EXPECT_TRUE(schema.empty());
    EXPECT_EQ(schema.encoded_size(), 0U);
    const std::vector<types::Value> values;
    EXPECT_TRUE(encode_row(schema, values)->empty());
    EXPECT_TRUE(decode_row(schema, {}).value().empty());
}

}  // namespace
}  // namespace db::access
