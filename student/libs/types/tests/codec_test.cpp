#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/types/codec.hpp"

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

TEST(CodecTest, HeapUsesStableLittleEndianBaselines) {
    const auto boolean = heap_encode(Value::boolean(true), kBoolType);
    const auto integer = heap_encode(Value::integer(0x0102'0304'0506'0708LL), kInt64Type);
    const auto floating = heap_encode(Value::floating(1.0F), kFloat32Type);
    const auto timestamp = heap_encode(Value::timestamp(TimestampValue{-2}), kTimestampType);

    ASSERT_TRUE(boolean);
    EXPECT_EQ(boolean.value(), bytes({0x01}));
    ASSERT_TRUE(integer);
    EXPECT_EQ(integer.value(), bytes({0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}));
    ASSERT_TRUE(floating);
    EXPECT_EQ(floating.value(), bytes({0x00, 0x00, 0x80, 0x3f}));
    ASSERT_TRUE(timestamp);
    EXPECT_EQ(timestamp.value(), bytes({0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}));
}

TEST(CodecTest, HeapEncodeIntoFillsCallerOwnedExactWidthStorage) {
    std::vector<std::byte> output(16U, std::byte{0xff});
    output.resize(6U);
    auto character = heap_encode_into(
        Value::character("你a", TypeModifier{6}),
        DataType{kCharTypeId, TypeModifier{6}},
        base::MutableBytes{output}
    );
    ASSERT_TRUE(character) << character.error().to_string();
    EXPECT_EQ(output, bytes({0xe4, 0xbd, 0xa0, 0x61, 0x00, 0x00}));

    output.resize(sizeof(std::int64_t));
    auto integer = heap_encode_into(Value::integer(7), kInt64Type, base::MutableBytes{output});
    ASSERT_TRUE(integer) << integer.error().to_string();
    EXPECT_EQ(output, bytes({0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}));

    output.resize(1U);
    auto wrong_width = heap_encode_into(Value::integer(7), kInt64Type, base::MutableBytes{output});
    ASSERT_FALSE(wrong_width);
    EXPECT_EQ(wrong_width.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(CodecTest, PreparedHeapEncoderCachesTypeAndWidthWithoutTrustingValues) {
    const DataType character_type{kCharTypeId, TypeModifier{6}};
    auto character = PreparedHeapEncoder::create(character_type);
    auto integer = PreparedHeapEncoder::create(kInt64Type);
    ASSERT_TRUE(character);
    ASSERT_TRUE(integer);
    EXPECT_EQ(character->data_type(), character_type);
    EXPECT_EQ(character->width(), 6U);

    std::vector<std::byte> output(character->width(), std::byte{0xff});
    auto encoded = character->encode_into(
        Value::character("你a", TypeModifier{6}),
        base::MutableBytes{output}
    );
    ASSERT_TRUE(encoded) << encoded.error().to_string();
    EXPECT_EQ(output, bytes({0xe4, 0xbd, 0xa0, 0x61, 0x00, 0x00}));

    EXPECT_FALSE(character->encode_into(Value::null(character_type), base::MutableBytes{output}));
    EXPECT_FALSE(character->encode_into(Value::integer(7), base::MutableBytes{output}));
    EXPECT_FALSE(character->encode_into(
        Value::character("你a", TypeModifier{6}),
        base::MutableBytes{output}.first(5U)
    ));
    const std::string invalid_utf8{"\xc0\xaf", 2};
    EXPECT_FALSE(character->encode_into(
        Value::character(invalid_utf8, TypeModifier{6}),
        base::MutableBytes{output}
    ));
    EXPECT_FALSE(PreparedHeapEncoder::create(DataType{kCharTypeId, TypeModifier{0}}));
}

TEST(CodecTest, PreparedHeapDecoderCachesTypeWidthAndConcreteDecoder) {
    const auto nan = std::bit_cast<float>(std::uint32_t{0xffc1'2345U});
    const DataType timestamp_type{kTimestampTypeId, TypeModifier{3}};

    struct Fixture final {
        DataType data_type;
        std::vector<std::byte> encoded;
        Value expected;
    };

    const std::vector<Fixture> fixtures{
        Fixture{kBoolType, bytes({0x01}), Value::boolean(true)},
        Fixture{
            kInt64Type,
            bytes({0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}),
            Value::integer(0x0102'0304'0506'0708LL),
        },
        Fixture{kFloat32Type, bytes({0x45, 0x23, 0xc1, 0xff}), Value::floating(nan)},
        Fixture{
            timestamp_type,
            bytes({0x88, 0x1f, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff}),
            Value::timestamp(TimestampValue{-123'000}, timestamp_type.modifier()),
        },
    };

    for (const auto& fixture : fixtures) {
        auto decoder = PreparedHeapDecoder::create(fixture.data_type);
        ASSERT_TRUE(decoder) << decoder.error().to_string();
        EXPECT_EQ(decoder->data_type(), fixture.data_type);
        EXPECT_EQ(decoder->width(), fixture.encoded.size());
        auto decoded = decoder->decode_exact(fixture.encoded);
        ASSERT_TRUE(decoded) << decoded.error().to_string();
        EXPECT_EQ(decoded.value(), fixture.expected);
    }
}

TEST(CodecTest, PreparedHeapDecoderRetainsExactWidthAndCanonicalInputErrors) {
    auto integer = PreparedHeapDecoder::create(kInt64Type);
    auto boolean = PreparedHeapDecoder::create(kBoolType);
    const DataType character_type{kCharTypeId, TypeModifier{6}};
    auto character = PreparedHeapDecoder::create(character_type);
    auto short_character = PreparedHeapDecoder::create(DataType{kCharTypeId, TypeModifier{3}});
    ASSERT_TRUE(integer);
    ASSERT_TRUE(boolean);
    ASSERT_TRUE(character);
    ASSERT_TRUE(short_character);

    auto text = character->decode_exact(bytes({0xe4, 0xbd, 0xa0, 0x61, 0x00, 0x00}));
    ASSERT_TRUE(text) << text.error().to_string();
    EXPECT_EQ(text.value(), Value::character("你a", character_type.modifier()));

    const auto truncated = integer->decode_exact(bytes({0x01, 0x02}));
    const auto trailing = integer->decode_exact(bytes({0, 0, 0, 0, 0, 0, 0, 0, 0}));
    const auto invalid_bool = boolean->decode_exact(bytes({0x02}));
    const auto invalid_utf8 = character->decode_exact(bytes({0xc0, 0xaf, 0, 0, 0, 0}));
    const auto partial_utf8 = short_character->decode_exact(bytes({0xe4, 0xbd, 0x00}));

    ASSERT_FALSE(truncated);
    EXPECT_EQ(truncated.error().code(), base::ErrorCode::kCorruption);
    ASSERT_FALSE(trailing);
    EXPECT_EQ(trailing.error().code(), base::ErrorCode::kCorruption);
    ASSERT_FALSE(invalid_bool);
    EXPECT_EQ(invalid_bool.error().code(), base::ErrorCode::kCorruption);
    ASSERT_FALSE(invalid_utf8);
    EXPECT_EQ(invalid_utf8.error().code(), base::ErrorCode::kCorruption);
    ASSERT_FALSE(partial_utf8);
    EXPECT_EQ(partial_utf8.error().code(), base::ErrorCode::kCorruption);
    EXPECT_FALSE(PreparedHeapDecoder::create(kCharType));
    EXPECT_FALSE(PreparedHeapDecoder::create(DataType{kCharTypeId, TypeModifier{0}}));
}

TEST(CodecTest, CanonicalFastPathsRetainBoundsAndStableEncoding) {
    const DataType character_type{kCharTypeId, TypeModifier{6}};
    auto character = PreparedHeapEncoder::create(character_type);
    ASSERT_TRUE(character);
    std::vector<std::byte> heap(character->width(), std::byte{0xff});
    ASSERT_TRUE(character->encode_canonical_into(
        Value::character("你a", character_type.modifier()),
        base::MutableBytes{heap}
    ));
    EXPECT_EQ(heap, bytes({0xe4, 0xbd, 0xa0, 0x61, 0x00, 0x00}));
    EXPECT_FALSE(character->encode_canonical_into(
        Value::character("1234567", character_type.modifier()),
        base::MutableBytes{heap}
    ));

    std::vector<std::byte> key;
    ASSERT_TRUE(key_encode_canonical_into(Value::character("你a", character_type.modifier()), key));
    EXPECT_EQ(key, bytes({0, 4, 0xe4, 0xbd, 0xa0, 0x61}));
    EXPECT_FALSE(
        key_encode_canonical_into(Value::character("1234567", character_type.modifier()), key)
    );
}

TEST(CodecTest, HeapCharIsFixedWidthAndDecodeReportsConsumption) {
    const DataType type{kCharTypeId, TypeModifier{6}};
    const auto encoded = heap_encode(Value::character("你a", TypeModifier{6}), type);
    ASSERT_TRUE(encoded);
    EXPECT_EQ(encoded.value(), bytes({0xe4, 0xbd, 0xa0, 0x61, 0x00, 0x00}));

    auto input = encoded.value();
    input.push_back(std::byte{0xff});
    const auto decoded = heap_decode(input, type);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().value, Value::character("你a", TypeModifier{6}));
    EXPECT_EQ(decoded.value().bytes_consumed, 6U);
}

TEST(CodecTest, HeapRoundTripsExactFloatBitsAndTimestampModifier) {
    const auto nan = std::bit_cast<float>(std::uint32_t{0xffc1'2345U});
    const DataType timestamp_type{kTimestampTypeId, TypeModifier{3}};
    const std::vector<Value> values{
        Value::integer(std::numeric_limits<std::int64_t>::min()),
        Value::floating(nan),
        Value::timestamp(TimestampValue{-123'000}, timestamp_type.modifier()),
    };

    for (const auto& value : values) {
        const auto encoded = heap_encode(value, value.data_type());
        ASSERT_TRUE(encoded);
        const auto decoded = heap_decode(encoded.value(), value.data_type());
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded.value().value, value);
        EXPECT_EQ(decoded.value().bytes_consumed, encoded.value().size());
    }
}

TEST(CodecTest, HeapRejectsNonCanonicalValuesAndCorruptInput) {
    const auto null_value = heap_encode(Value::null(kInt64Type), kInt64Type);
    const auto mismatched = heap_encode(Value::integer(1), kFloat32Type);
    const auto unbounded = heap_encode(Value::character("a"), kCharType);
    const auto too_long = heap_encode(
        Value::character("ab", TypeModifier{1}),
        DataType{kCharTypeId, TypeModifier{1}}
    );
    const auto truncated = heap_decode(bytes({0x01, 0x02}), kInt64Type);
    const auto invalid_bool = heap_decode(bytes({0x02}), kBoolType);
    const auto invalid_utf8 =
        heap_decode(bytes({0xc0, 0xaf, 0x00}), DataType{kCharTypeId, TypeModifier{3}});

    EXPECT_FALSE(null_value);
    EXPECT_FALSE(mismatched);
    EXPECT_FALSE(unbounded);
    EXPECT_FALSE(too_long);
    ASSERT_FALSE(truncated);
    EXPECT_EQ(truncated.error().code(), base::ErrorCode::kCorruption);
    ASSERT_FALSE(invalid_bool);
    EXPECT_EQ(invalid_bool.error().code(), base::ErrorCode::kCorruption);
    ASSERT_FALSE(invalid_utf8);
    EXPECT_EQ(invalid_utf8.error().code(), base::ErrorCode::kCorruption);
}

TEST(CodecTest, RejectsForgedNonCanonicalCharacterValues) {
    const std::string invalid_utf8{"\xc0\xaf", 2};
    const auto invalid_heap = heap_encode(
        Value::character(invalid_utf8, TypeModifier{3}),
        DataType{kCharTypeId, TypeModifier{3}}
    );
    const auto invalid_key = key_encode(Value::character(invalid_utf8, TypeModifier{3}));
    const auto too_long_key = key_encode(Value::character("ab", TypeModifier{1}));

    ASSERT_FALSE(invalid_heap);
    EXPECT_EQ(invalid_heap.error().code(), base::ErrorCode::kInvalidArgument);
    ASSERT_FALSE(invalid_key);
    EXPECT_EQ(invalid_key.error().code(), base::ErrorCode::kInvalidArgument);
    ASSERT_FALSE(too_long_key);
    EXPECT_EQ(too_long_key.error().code(), base::ErrorCode::kOutOfRange);
}

TEST(CodecTest, OrderedIntegerAndTimestampKeysSortLexicographically) {
    const std::vector<Value> integers{
        Value::integer(std::numeric_limits<std::int64_t>::min()),
        Value::integer(-1),
        Value::integer(0),
        Value::integer(std::numeric_limits<std::int64_t>::max()),
    };
    std::vector<std::byte> previous;
    for (const auto& value : integers) {
        const auto encoded = key_encode(value);
        ASSERT_TRUE(encoded);
        if (!previous.empty()) {
            const auto ordering = key_compare(previous, encoded.value(), kInt64Type);
            ASSERT_TRUE(ordering);
            EXPECT_EQ(ordering.value(), std::strong_ordering::less);
        }
        previous = encoded.value();
    }
    EXPECT_EQ(key_encode(Value::integer(0)).value(), bytes({0x80, 0, 0, 0, 0, 0, 0, 0}));

    const auto before = key_encode(Value::timestamp(TimestampValue{-1}));
    const auto after = key_encode(Value::timestamp(TimestampValue{1}));
    ASSERT_TRUE(before);
    ASSERT_TRUE(after);
    EXPECT_EQ(
        key_compare(before.value(), after.value(), kTimestampType).value(),
        std::strong_ordering::less
    );
}

TEST(CodecTest, FloatKeysNormalizeNanAndPreserveTotalByteOrder) {
    const auto negative_nan = std::bit_cast<float>(std::uint32_t{0xffc0'1234U});
    const auto positive_nan = std::bit_cast<float>(std::uint32_t{0x7fa0'5678U});
    const auto negative = key_encode(Value::floating(-1.0F));
    const auto zero = key_encode(Value::floating(0.0F));
    const auto positive = key_encode(Value::floating(1.0F));
    const auto left_nan = key_encode(Value::floating(negative_nan));
    const auto right_nan = key_encode(Value::floating(positive_nan));

    ASSERT_TRUE(negative);
    ASSERT_TRUE(zero);
    ASSERT_TRUE(positive);
    ASSERT_TRUE(left_nan);
    ASSERT_TRUE(right_nan);
    EXPECT_EQ(
        key_compare(negative.value(), zero.value(), kFloat32Type).value(),
        std::strong_ordering::less
    );
    EXPECT_EQ(
        key_compare(zero.value(), positive.value(), kFloat32Type).value(),
        std::strong_ordering::less
    );
    EXPECT_EQ(left_nan.value(), right_nan.value());
    EXPECT_EQ(left_nan.value(), bytes({0xff, 0xc0, 0x00, 0x00}));
}

TEST(CodecTest, CharacterKeysUsePayloadOrderingNotLengthOrdering) {
    const auto shorter = key_encode(Value::character("b", TypeModifier{4}));
    const auto longer = key_encode(Value::character("aa", TypeModifier{4}));
    ASSERT_TRUE(shorter);
    ASSERT_TRUE(longer);
    EXPECT_EQ(shorter.value(), bytes({0, 1, 0x62}));
    EXPECT_EQ(longer.value(), bytes({0, 2, 0x61, 0x61}));
    EXPECT_EQ(
        key_compare(shorter.value(), longer.value(), DataType{kCharTypeId, TypeModifier{4}})
            .value(),
        std::strong_ordering::greater
    );
    EXPECT_EQ(key_encoded_size(longer.value(), kCharType).value(), 4U);
    EXPECT_EQ(key_payload_size(Value::character("aa", TypeModifier{4})).value(), 4U);
    EXPECT_EQ(key_max_size(DataType{kCharTypeId, TypeModifier{4}}).value(), 6U);
}

TEST(CodecTest, KeyEncodeIntoAppendsWithoutChangingFailureInput) {
    std::vector<std::byte> output{std::byte{0xff}};
    ASSERT_TRUE(key_encode_into(Value::integer(0), output));
    ASSERT_TRUE(key_encode_into(Value::character("aa", TypeModifier{4}), output));
    EXPECT_EQ(output, bytes({0xff, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0x61, 0x61}));

    const auto before = output;
    auto null_value = key_encode_into(Value::null(kInt64Type), output);
    ASSERT_FALSE(null_value);
    EXPECT_EQ(output, before);
}

TEST(CodecTest, KeyHooksRejectNullUnsupportedAndTruncatedComponents) {
    const auto null_value = key_encode(Value::null(kBoolType));
    const auto unknown = key_encode(Value::unknown_string("literal"));
    const auto unknown_size = key_encoded_size({}, kUnknownType);
    const auto unbounded_max = key_max_size(kCharType);
    const auto truncated_char = key_encoded_size(bytes({0, 2, 0x61}), kCharType);
    const auto invalid_bool = key_encoded_size(bytes({2}), kBoolType);

    EXPECT_FALSE(null_value);
    EXPECT_FALSE(unknown);
    EXPECT_FALSE(unknown_size);
    EXPECT_FALSE(unbounded_max);
    ASSERT_FALSE(truncated_char);
    EXPECT_EQ(truncated_char.error().code(), base::ErrorCode::kCorruption);
    ASSERT_FALSE(invalid_bool);
    EXPECT_EQ(invalid_bool.error().code(), base::ErrorCode::kCorruption);
}

TEST(CodecTest, KeyReadersRejectNoncanonicalFloatAndCharacterPayloads) {
    const auto noncanonical_nan = key_encoded_size(bytes({0xff, 0xc0, 0x12, 0x34}), kFloat32Type);
    const auto invalid_utf8 = key_encoded_size(bytes({0, 2, 0xc0, 0xaf}), kCharType);
    const auto over_width =
        key_encoded_size(bytes({0, 2, 0x61, 0x62}), DataType{kCharTypeId, TypeModifier{1}});
    const auto invalid_compare = key_compare(
        bytes({0, 2, 0xc0, 0xaf}),
        bytes({0, 1, 0x61}),
        DataType{kCharTypeId, TypeModifier{4}}
    );

    ASSERT_FALSE(noncanonical_nan);
    EXPECT_EQ(noncanonical_nan.error().code(), base::ErrorCode::kCorruption);
    ASSERT_FALSE(invalid_utf8);
    EXPECT_EQ(invalid_utf8.error().code(), base::ErrorCode::kCorruption);
    ASSERT_FALSE(over_width);
    EXPECT_EQ(over_width.error().code(), base::ErrorCode::kCorruption);
    ASSERT_FALSE(invalid_compare);
    EXPECT_EQ(invalid_compare.error().code(), base::ErrorCode::kCorruption);
}

}  // namespace
}  // namespace db::types
