#include "db/types/codec.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/types/type_registry.hpp"

#include "type_io_internal.hpp"

namespace db::types {
namespace {

inline constexpr std::uint32_t kFloatSign = std::uint32_t{1} << 31U;
inline constexpr std::uint32_t kFloatExponent = 0x7f80'0000U;
inline constexpr std::uint32_t kFloatFraction = 0x007f'ffffU;
inline constexpr std::uint32_t kCanonicalFloatNan = 0x7fc0'0000U;
inline constexpr std::uint32_t kCanonicalFloatNanKey = kCanonicalFloatNan ^ kFloatSign;

auto codec_error(
    const base::ErrorCode code,
    const std::string_view format,
    const std::string_view detail
) -> base::Error {
    return {code, std::string{format} + ": " + std::string{detail}};
}

auto validate_non_null_value(
    const Value& value,
    const DataType data_type,
    const std::string_view format
) -> base::Status {
    auto validation = validate_data_type(data_type);
    if (!validation) {
        return base::fail(std::move(validation).error().with_context("types", std::string{format}));
    }
    if (value.is_null()) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            format,
            "NULL markers belong to the containing record"
        ));
    }
    if (value.data_type() != data_type) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            format,
            "value and declared data types differ"
        ));
    }
    return {};
}

auto canonical_character(
    const Value& value,
    const DataType data_type,
    const std::string_view format
) -> base::Result<std::string_view> {
    const auto* const text = value.as_string();
    if (text == nullptr) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            format,
            "CHAR codec received a non-string payload"
        ));
    }
    return validate_canonical_character(*text, data_type, format);
}

constexpr auto ordered_i64(const std::int64_t value) noexcept -> std::uint64_t {
    return std::bit_cast<std::uint64_t>(value) ^ (std::uint64_t{1} << 63U);
}

constexpr auto ordered_float32(const float value) noexcept -> std::uint32_t {
    auto bits = std::bit_cast<std::uint32_t>(value);
    if ((bits & kFloatExponent) == kFloatExponent && (bits & kFloatFraction) != 0U) {
        bits = kCanonicalFloatNan;
    }
    return (bits & kFloatSign) != 0U ? ~bits : bits ^ kFloatSign;
}

constexpr auto is_float_nan_bits(const std::uint32_t bits) noexcept -> bool {
    return (bits & kFloatExponent) == kFloatExponent && (bits & kFloatFraction) != 0U;
}

constexpr auto decode_ordered_float32(const std::uint32_t ordered) noexcept -> std::uint32_t {
    return (ordered & kFloatSign) != 0U ? ordered ^ kFloatSign : ~ordered;
}

auto key_payload_mismatch() -> base::Error {
    return codec_error(
        base::ErrorCode::kInvalidArgument,
        "B+Tree key encode",
        "value payload does not match its type"
    );
}

auto append_key_payload(
    const Value& value,
    std::vector<std::byte>& output,
    const bool validate_character
) -> base::Status {
    base::BigEndianAppender appender{output};
    if (value.data_type().id() == kBoolTypeId) {
        const auto* const boolean = value.as_boolean();
        if (boolean == nullptr) {
            return base::fail(key_payload_mismatch());
        }
        appender.append_u8(*boolean ? std::uint8_t{1U} : std::uint8_t{0U});
        return {};
    }
    if (value.data_type().id() == kInt64TypeId) {
        const auto* const integer = value.as_integer();
        if (integer == nullptr) {
            return base::fail(key_payload_mismatch());
        }
        appender.append_u64(ordered_i64(*integer));
        return {};
    }
    if (value.data_type().id() == kFloat32TypeId) {
        const auto* const floating = value.as_float();
        if (floating == nullptr) {
            return base::fail(key_payload_mismatch());
        }
        appender.append_u32(ordered_float32(*floating));
        return {};
    }
    if (value.data_type().id() == kTimestampTypeId) {
        const auto* const timestamp = value.as_timestamp();
        if (timestamp == nullptr) {
            return base::fail(key_payload_mismatch());
        }
        appender.append_u64(ordered_i64(timestamp->microseconds()));
        return {};
    }
    if (value.data_type().id() != kCharTypeId) {
        return base::fail(key_payload_mismatch());
    }

    const auto* const character = value.as_string();
    if (character == nullptr) {
        return base::fail(key_payload_mismatch());
    }
    std::string_view text_value{*character};
    if (validate_character) {
        auto text = canonical_character(value, value.data_type(), "B+Tree key encode");
        if (!text) {
            return base::fail(std::move(text).error());
        }
        text_value = text.value();
    } else if (
        !value.data_type().modifier().is_none()
        && text_value.size() > static_cast<std::size_t>(value.data_type().modifier().value())
    ) {
        return base::fail(codec_error(
            base::ErrorCode::kOutOfRange,
            "B+Tree key encode",
            "CHAR payload exceeds its byte modifier"
        ));
    }
    if (text_value.size() > std::numeric_limits<std::uint16_t>::max()) {
        return base::fail(codec_error(
            base::ErrorCode::kOutOfRange,
            "B+Tree key encode",
            "CHAR payload exceeds the 16-bit length field"
        ));
    }
    const auto length = static_cast<std::uint16_t>(text_value.size());
    appender.append_u16(length);
    const auto text_bytes = std::as_bytes(std::span{text_value.data(), text_value.size()});
    appender.append_bytes(text_bytes);
    return {};
}

auto fixed_key_size(const base::Bytes input, const std::size_t width) -> base::Result<std::size_t> {
    if (input.size() < width) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "B+Tree key component is truncated",
            }
        );
    }
    return width;
}

auto canonical_float_key_size(const base::Bytes input) -> base::Result<std::size_t> {
    auto size = fixed_key_size(input, sizeof(std::uint32_t));
    if (!size) {
        return size;
    }

    base::BigEndianReader reader{input.first(size.value())};
    auto ordered = reader.read_u32();
    if (!ordered) {
        return base::fail(std::move(ordered).error().with_context("types", "key size"));
    }
    if (is_float_nan_bits(decode_ordered_float32(ordered.value()))
        && ordered.value() != kCanonicalFloatNanKey) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "FLOAT key contains a noncanonical NaN encoding",
            }
        );
    }
    return size;
}

auto byte_string_view(const base::Bytes input) noexcept -> std::string_view {
    if (input.empty()) {
        return {};
    }
    // CHAR key bytes are text octets, not a native object representation. Character types may
    // inspect any object's bytes, so this preserves the bounded span without an allocation.
    // Character types may alias object bytes; the returned view remains bounded by the input span.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* const characters = reinterpret_cast<const char*>(input.data());
    return {characters, input.size()};
}

auto canonical_character_key_size(const base::Bytes input, const DataType data_type)
    -> base::Result<std::size_t> {
    base::BigEndianReader reader{input};
    auto length = reader.read_u16();
    if (!length) {
        return base::fail(std::move(length).error().with_context("types", "key size"));
    }
    auto payload = reader.read_bytes(length.value());
    if (!payload) {
        return base::fail(std::move(payload).error().with_context("types", "key size"));
    }
    auto canonical =
        validate_canonical_character(byte_string_view(payload.value()), data_type, "key size");
    if (!canonical) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "CHAR key payload is invalid for its declared type",
            }
                .with_context("types", "key size")
        );
    }
    return reader.position();
}

auto compare_bytes(const base::Bytes left, const base::Bytes right) noexcept
    -> std::strong_ordering {
    const auto shared = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < shared; ++index) {
        const auto left_byte = std::to_integer<std::uint8_t>(left[index]);
        const auto right_byte = std::to_integer<std::uint8_t>(right[index]);
        if (left_byte < right_byte) {
            return std::strong_ordering::less;
        }
        if (left_byte > right_byte) {
            return std::strong_ordering::greater;
        }
    }
    return left.size() <=> right.size();
}

auto encode_fixed_heap_into(const Value& value, const base::MutableBytes output) -> base::Status {
    if (value.data_type().id() == kBoolTypeId && value.as_boolean() != nullptr) {
        output.front() = *value.as_boolean() ? std::byte{1} : std::byte{0};
        return {};
    }

    if (value.data_type().id() == kFloat32TypeId && value.as_float() != nullptr) {
        base::LittleEndianWriter writer{output};
        auto written = writer.write_u32(std::bit_cast<std::uint32_t>(*value.as_float()));
        if (!written) {
            return base::fail(std::move(written).error());
        }
        return {};
    }

    std::int64_t encoded = 0;
    if (value.data_type().id() == kInt64TypeId && value.as_integer() != nullptr) {
        encoded = *value.as_integer();
    } else if (value.data_type().id() == kTimestampTypeId && value.as_timestamp() != nullptr) {
        encoded = value.as_timestamp()->microseconds();
    } else {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            "heap codec",
            "value payload does not match its type"
        ));
    }
    base::LittleEndianWriter writer{output};
    auto written = writer.write_i64(encoded);
    if (!written) {
        return base::fail(std::move(written).error());
    }
    return {};
}

auto encode_heap_payload_into(
    const Value& value,
    const DataType data_type,
    const base::MutableBytes output,
    const bool validate_character
) -> base::Status {
    if (data_type.id() != kCharTypeId) {
        return encode_fixed_heap_into(value, output);
    }
    const auto* const character = value.as_string();
    if (character == nullptr) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            "heap codec",
            "value payload does not match its type"
        ));
    }
    std::string_view text_value{*character};
    if (validate_character) {
        auto text = canonical_character(value, data_type, "heap encode");
        if (!text) {
            return base::fail(std::move(text).error());
        }
        text_value = text.value();
    } else if (text_value.size() > output.size()) {
        return base::fail(codec_error(
            base::ErrorCode::kOutOfRange,
            "heap encode",
            "CHAR payload exceeds its byte modifier"
        ));
    }
    std::fill(output.begin(), output.end(), std::byte{0});
    const auto text_bytes = std::as_bytes(std::span{text_value.data(), text_value.size()});
    std::copy(text_bytes.begin(), text_bytes.end(), output.begin());
    return {};
}

auto load_u32_le(const base::Bytes input) noexcept -> std::uint32_t {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        value |= std::to_integer<std::uint32_t>(input[index]) << (index * 8U);
    }
    return value;
}

auto load_u64_le(const base::Bytes input) noexcept -> std::uint64_t {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        value |= std::to_integer<std::uint64_t>(input[index]) << (index * 8U);
    }
    return value;
}

auto decode_bool_heap_exact(const base::Bytes input, const DataType data_type)
    -> base::Result<Value> {
    static_cast<void>(data_type);
    const auto encoded = std::to_integer<std::uint8_t>(input.front());
    if (encoded > 1U) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "BOOL heap byte is neither zero nor one",
            }
        );
    }
    return Value::boolean(encoded != 0U);
}

auto decode_float32_heap_exact(const base::Bytes input, const DataType data_type)
    -> base::Result<Value> {
    static_cast<void>(data_type);
    return Value::floating(std::bit_cast<float>(load_u32_le(input)));
}

auto decode_int64_heap_exact(const base::Bytes input, const DataType data_type)
    -> base::Result<Value> {
    static_cast<void>(data_type);
    return Value::integer(std::bit_cast<std::int64_t>(load_u64_le(input)));
}

auto decode_timestamp_heap_exact(const base::Bytes input, const DataType data_type)
    -> base::Result<Value> {
    return Value::timestamp(
        TimestampValue{std::bit_cast<std::int64_t>(load_u64_le(input))},
        data_type.modifier()
    );
}

auto decode_character_heap_exact(const base::Bytes input, const DataType data_type)
    -> base::Result<Value> {
    std::size_t text_size = input.size();
    while (text_size > 0U && input[text_size - 1U] == std::byte{0}) {
        --text_size;
    }
    const auto text = byte_string_view(input.first(text_size));
    auto canonical = validate_canonical_character(text, data_type, "heap decode");
    if (!canonical) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "CHAR heap payload is not canonical UTF-8",
            }
        );
    }
    return Value::character(std::string{text}, data_type.modifier());
}

}  // namespace

auto PreparedHeapEncoder::create(const DataType data_type) -> base::Result<PreparedHeapEncoder> {
    auto validation = validate_data_type(data_type);
    if (!validation) {
        return base::fail(
            std::move(validation).error().with_context("types", "prepare heap encoder")
        );
    }
    auto width = storage_width(data_type);
    if (!width) {
        return base::fail(std::move(width).error().with_context("types", "prepare heap encoder"));
    }
    return PreparedHeapEncoder{data_type, width.value()};
}

auto PreparedHeapEncoder::encode_into(const Value& value, const base::MutableBytes output) const
    -> base::Status {
    if (value.is_null()) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            "heap encode",
            "NULL markers belong to the containing record"
        ));
    }
    if (value.data_type() != data_type_) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            "heap encode",
            "value and declared data types differ"
        ));
    }
    if (output.size() != width_) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            "heap encode",
            "output width does not match the declared data type"
        ));
    }
    return encode_heap_payload_into(value, data_type_, output, true);
}

auto PreparedHeapEncoder::encode_canonical_into(
    const Value& value,
    const base::MutableBytes output
) const -> base::Status {
    if (value.is_null()) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            "heap encode",
            "NULL markers belong to the containing record"
        ));
    }
    if (value.data_type() != data_type_) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            "heap encode",
            "value and declared data types differ"
        ));
    }
    if (output.size() != width_) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            "heap encode",
            "output width does not match the declared data type"
        ));
    }
    return encode_heap_payload_into(value, data_type_, output, false);
}

auto PreparedHeapEncoder::data_type() const noexcept -> DataType {
    return data_type_;
}

auto PreparedHeapEncoder::width() const noexcept -> std::size_t {
    return width_;
}

auto PreparedHeapDecoder::create(const DataType data_type) -> base::Result<PreparedHeapDecoder> {
    auto width = storage_width(data_type);
    if (!width) {
        return base::fail(std::move(width).error().with_context("types", "prepare heap decoder"));
    }

    DecodeFunction decode = nullptr;
    if (data_type.id() == kBoolTypeId) {
        decode = &decode_bool_heap_exact;
    } else if (data_type.id() == kInt64TypeId) {
        decode = &decode_int64_heap_exact;
    } else if (data_type.id() == kFloat32TypeId) {
        decode = &decode_float32_heap_exact;
    } else if (data_type.id() == kTimestampTypeId) {
        decode = &decode_timestamp_heap_exact;
    } else if (data_type.id() == kCharTypeId) {
        decode = &decode_character_heap_exact;
    } else {
        return base::fail(codec_error(
            base::ErrorCode::kInvariantViolation,
            "heap decode",
            "storable type has no decoder"
        ));
    }
    return PreparedHeapDecoder{data_type, width.value(), decode};
}

auto PreparedHeapDecoder::decode_exact(const base::Bytes input) const -> base::Result<Value> {
    if (input.size() < width_) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "heap value is truncated",
            }
        );
    }
    if (input.size() > width_) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "heap value has trailing data",
            }
        );
    }
    return decode_(input, data_type_);
}

auto PreparedHeapDecoder::data_type() const noexcept -> DataType {
    return data_type_;
}

auto PreparedHeapDecoder::width() const noexcept -> std::size_t {
    return width_;
}

auto heap_encode_into(const Value& value, const DataType data_type, const base::MutableBytes output)
    -> base::Status {
    auto validated = validate_non_null_value(value, data_type, "heap encode");
    if (!validated) {
        return base::fail(std::move(validated).error());
    }
    auto width = storage_width(data_type);
    if (!width) {
        return base::fail(std::move(width).error().with_context("types", "heap encode"));
    }
    if (output.size() != width.value()) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            "heap encode",
            "output width does not match the declared data type"
        ));
    }
    return encode_heap_payload_into(value, data_type, output, true);
}

auto heap_encode(const Value& value, const DataType data_type)
    -> base::Result<std::vector<std::byte>> {
    auto validated = validate_non_null_value(value, data_type, "heap encode");
    if (!validated) {
        return base::fail(std::move(validated).error());
    }
    auto width = storage_width(data_type);
    if (!width) {
        return base::fail(std::move(width).error().with_context("types", "heap encode"));
    }
    std::vector<std::byte> output(width.value(), std::byte{0});
    auto encoded = encode_heap_payload_into(value, data_type, base::MutableBytes{output}, true);
    if (!encoded) {
        return base::fail(std::move(encoded).error());
    }
    return output;
}

auto heap_decode(const base::Bytes input, const DataType data_type)
    -> base::Result<HeapDecodedValue> {
    auto decoder = PreparedHeapDecoder::create(data_type);
    if (!decoder) {
        return base::fail(std::move(decoder).error().with_context("types", "heap decode"));
    }
    if (input.size() < decoder->width()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "heap value is truncated",
            }
        );
    }
    auto value = decoder->decode_exact(input.first(decoder->width()));
    if (!value) {
        return base::fail(std::move(value).error());
    }
    return HeapDecodedValue{std::move(value).value(), decoder->width()};
}

auto key_encode_into(const Value& value, std::vector<std::byte>& output) -> base::Status {
    auto validated = validate_non_null_value(value, value.data_type(), "B+Tree key encode");
    if (!validated) {
        return validated;
    }
    return append_key_payload(value, output, true);
}

auto key_encode_canonical_into(const Value& value, std::vector<std::byte>& output) -> base::Status {
    if (value.is_null()) {
        return base::fail(codec_error(
            base::ErrorCode::kInvalidArgument,
            "B+Tree key encode",
            "NULL markers belong to the containing record"
        ));
    }
    return append_key_payload(value, output, false);
}

auto key_encode(const Value& value) -> base::Result<std::vector<std::byte>> {
    std::vector<std::byte> output;
    auto encoded = key_encode_into(value, output);
    if (!encoded) {
        return base::fail(std::move(encoded).error());
    }
    return output;
}

auto key_payload_size(const Value& value) -> base::Result<std::size_t> {
    auto validated = validate_non_null_value(value, value.data_type(), "B+Tree key encode");
    if (!validated) {
        return base::fail(std::move(validated).error());
    }
    if (value.data_type().id() == kBoolTypeId) {
        return value.as_boolean() == nullptr
                   ? base::Result<std::size_t>{base::fail(key_payload_mismatch())}
                   : base::Result<std::size_t>{1U};
    }
    if (value.data_type().id() == kInt64TypeId) {
        return value.as_integer() == nullptr
                   ? base::Result<std::size_t>{base::fail(key_payload_mismatch())}
                   : base::Result<std::size_t>{sizeof(std::uint64_t)};
    }
    if (value.data_type().id() == kFloat32TypeId) {
        return value.as_float() == nullptr
                   ? base::Result<std::size_t>{base::fail(key_payload_mismatch())}
                   : base::Result<std::size_t>{sizeof(std::uint32_t)};
    }
    if (value.data_type().id() == kTimestampTypeId) {
        return value.as_timestamp() == nullptr
                   ? base::Result<std::size_t>{base::fail(key_payload_mismatch())}
                   : base::Result<std::size_t>{sizeof(std::uint64_t)};
    }
    if (value.data_type().id() != kCharTypeId) {
        return base::fail(key_payload_mismatch());
    }
    auto text = canonical_character(value, value.data_type(), "B+Tree key encode");
    if (!text) {
        return base::fail(std::move(text).error());
    }
    if (text->size() > std::numeric_limits<std::uint16_t>::max()) {
        return base::fail(codec_error(
            base::ErrorCode::kOutOfRange,
            "B+Tree key encode",
            "CHAR payload exceeds the 16-bit length field"
        ));
    }
    return sizeof(std::uint16_t) + text->size();
}

auto key_encoded_size(const base::Bytes input, const DataType data_type)
    -> base::Result<std::size_t> {
    auto validation = validate_data_type(data_type);
    if (!validation) {
        return base::fail(std::move(validation).error().with_context("types", "key size"));
    }
    if (data_type.id() == kUnknownTypeId) {
        return base::fail(codec_error(
            base::ErrorCode::kUnsupported,
            "B+Tree key size",
            "UNKNOWN has no key representation"
        ));
    }
    if (data_type.id() == kBoolTypeId) {
        auto size = fixed_key_size(input, 1U);
        if (size && std::to_integer<std::uint8_t>(input.front()) > 1U) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kCorruption,
                    "BOOL key byte is neither zero nor one",
                }
            );
        }
        return size;
    }
    if (data_type.id() == kFloat32TypeId) {
        return canonical_float_key_size(input);
    }
    if (data_type.id() == kInt64TypeId || data_type.id() == kTimestampTypeId) {
        return fixed_key_size(input, sizeof(std::uint64_t));
    }
    if (data_type.id() == kCharTypeId) {
        return canonical_character_key_size(input, data_type);
    }
    return base::fail(codec_error(
        base::ErrorCode::kInvariantViolation,
        "B+Tree key size",
        "validated type has no key codec"
    ));
}

auto key_compare(const base::Bytes left, const base::Bytes right, const DataType data_type)
    -> base::Result<std::strong_ordering> {
    auto left_size = key_encoded_size(left, data_type);
    if (!left_size) {
        return base::fail(std::move(left_size).error().with_context("types", "key compare left"));
    }
    auto right_size = key_encoded_size(right, data_type);
    if (!right_size) {
        return base::fail(std::move(right_size).error().with_context("types", "key compare right"));
    }
    auto left_component = left.first(left_size.value());
    auto right_component = right.first(right_size.value());
    if (data_type.id() == kCharTypeId) {
        left_component = left_component.subspan(sizeof(std::uint16_t));
        right_component = right_component.subspan(sizeof(std::uint16_t));
    }
    return compare_bytes(left_component, right_component);
}

auto key_max_size(const DataType data_type) -> base::Result<std::size_t> {
    auto validation = validate_data_type(data_type);
    if (!validation) {
        return base::fail(std::move(validation).error().with_context("types", "key max size"));
    }
    if (data_type.id() == kBoolTypeId) {
        return 1U;
    }
    if (data_type.id() == kFloat32TypeId) {
        return sizeof(std::uint32_t);
    }
    if (data_type.id() == kInt64TypeId || data_type.id() == kTimestampTypeId) {
        return sizeof(std::uint64_t);
    }
    if (data_type.id() == kCharTypeId && !data_type.modifier().is_none()) {
        return sizeof(std::uint16_t) + static_cast<std::size_t>(data_type.modifier().value());
    }
    return base::fail(codec_error(
        base::ErrorCode::kUnsupported,
        "B+Tree key max size",
        "type has no finite key bound"
    ));
}

}  // namespace db::types
