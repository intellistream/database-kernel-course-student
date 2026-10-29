#include "db/types/type_io.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

#include "db/base/error.hpp"
#include "db/base/text.hpp"
#include "db/types/datetime.hpp"
#include "db/types/type_registry.hpp"

#include "type_io_internal.hpp"

namespace db::types {
namespace {

auto input_error(
    const std::string_view type_name,
    const std::string_view text,
    const std::string_view detail = {}
) -> base::Error {
    std::string message = "invalid input syntax for ";
    message.append(type_name);
    message.append(": \"");
    message.append(text);
    message.push_back('"');
    if (!detail.empty()) {
        message.append(": ");
        message.append(detail);
    }
    return {base::ErrorCode::kInvalidArgument, std::move(message)};
}

auto invalid_character_utf8() -> base::Error {
    return base::Error{
        base::ErrorCode::kInvalidArgument,
        "CHAR input is not valid UTF-8",
    };
}

auto truncate_char(const std::string_view text, const TypeModifier modifier)
    -> base::Result<std::string> {
    const auto maximum =
        modifier.is_none() ? text.size() : static_cast<std::size_t>(modifier.value());
    const auto prefix = base::valid_utf8_prefix_size(text, maximum);
    if (!prefix.has_value()) {
        return base::fail(invalid_character_utf8());
    }
    return std::string{text.substr(0, prefix.value())};
}

template <typename Number>
auto number_output(const Number value) -> std::string {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.begin(), buffer.end(), value);
    if (result.ec != std::errc{}) {
        return "<numeric-output-error>";
    }
    return std::string{buffer.begin(), result.ptr};
}

auto input_bool(const std::string_view input) -> base::Result<Value> {
    const auto text = base::trim_ascii_space(input);
    if (text == "1" || base::ascii_equal_ignoring_case(text, "t")
        || base::ascii_equal_ignoring_case(text, "true")) {
        return Value::boolean(true);
    }
    if (text == "0" || base::ascii_equal_ignoring_case(text, "f")
        || base::ascii_equal_ignoring_case(text, "false")) {
        return Value::boolean(false);
    }
    return base::fail(input_error("bool", input));
}

auto input_int64(const std::string_view input) -> base::Result<Value> {
    auto text = base::trim_ascii_space(input);
    bool explicitly_positive = false;
    if (!text.empty() && text.front() == '+') {
        explicitly_positive = true;
        text.remove_prefix(1);
    }
    if (text.empty() || (explicitly_positive && text.front() == '-')) {
        return base::fail(input_error("int", input, "expected a signed 64-bit integer"));
    }
    std::int64_t value = 0;
    const auto result = std::from_chars(text.begin(), text.end(), value);
    if (result.ec == std::errc::result_out_of_range) {
        return base::fail(input_error("int", input, "value out of range"));
    }
    if (result.ec != std::errc{} || result.ptr != text.end()) {
        return base::fail(input_error("int", input, "invalid digit found in string"));
    }
    return Value::integer(value);
}

auto input_float32(const std::string_view input) -> base::Result<Value> {
    auto text = base::trim_ascii_space(input);
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
    }
    float value = 0.0F;
    const auto result =
        std::from_chars(text.begin(), text.end(), value, std::chars_format::general);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.end()) {
        return base::fail(input_error("float", input, "value is outside binary32 range"));
    }
    return Value::floating(value);
}

auto input_timestamp_value(const DataType data_type, const std::string_view input)
    -> base::Result<Value> {
    auto parsed = parse_timestamp(input);
    if (!parsed) {
        return base::fail(input_error("timestamp", input, parsed.error().message()));
    }
    const auto precision = data_type.modifier().is_none()
                               ? kMaxTimestampPrecision
                               : static_cast<std::uint32_t>(data_type.modifier().value());
    auto adjusted = adjust_timestamp_precision(parsed.value(), precision);
    if (!adjusted) {
        return base::fail(std::move(adjusted).error());
    }
    return Value::timestamp(adjusted.value(), data_type.modifier());
}

auto parse_bool(const DataType data_type, const std::string_view input) -> base::Result<Value> {
    static_cast<void>(data_type);
    return input_bool(input);
}

auto parse_int64(const DataType data_type, const std::string_view input) -> base::Result<Value> {
    static_cast<void>(data_type);
    return input_int64(input);
}

auto parse_float32(const DataType data_type, const std::string_view input) -> base::Result<Value> {
    static_cast<void>(data_type);
    return input_float32(input);
}

auto parse_char(const DataType data_type, const std::string_view input) -> base::Result<Value> {
    auto truncated = truncate_char(input, data_type.modifier());
    if (!truncated) {
        return base::fail(std::move(truncated).error());
    }
    return Value::character(std::move(truncated).value(), data_type.modifier());
}

auto parse_timestamp(const DataType data_type, const std::string_view input)
    -> base::Result<Value> {
    return input_timestamp_value(data_type, input);
}

auto parse_unknown(const DataType data_type, const std::string_view input) -> base::Result<Value> {
    static_cast<void>(data_type);
    static_cast<void>(input);
    return base::fail(
        base::Error{
            base::ErrorCode::kInvalidArgument,
            "unknown does not accept stored text input",
        }
    );
}

auto type_mismatch(const Value& value, const DataType target) -> base::Result<Value> {
    return base::fail(
        base::Error{
            base::ErrorCode::kInvalidArgument,
            "value type " + std::to_string(value.data_type().id().value())
                + " does not match target type " + std::to_string(target.id().value()),
        }
    );
}

}  // namespace

auto validate_canonical_character(
    const std::string_view text,
    const DataType data_type,
    const std::string_view format
) -> base::Result<std::string_view> {
    if (data_type.id() != kCharTypeId) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "CHAR validation received a non-CHAR data type",
            }
        );
    }
    const auto maximum = data_type.modifier().is_none()
                             ? text.size()
                             : static_cast<std::size_t>(data_type.modifier().value());
    std::size_t position = 0U;
    while (position < text.size()) {
        const auto next = base::utf8_code_point_end(text, position);
        if (!next.has_value()) {
            return base::fail(invalid_character_utf8().with_context("types", std::string{format}));
        }
        position = next.value();
        if (position > maximum) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kOutOfRange,
                    "CHAR payload exceeds its byte modifier",
                }
                    .with_context("types", std::string{format})
            );
        }
    }
    return text;
}

auto PreparedTypeInput::create(const DataType data_type) -> base::Result<PreparedTypeInput> {
    auto validation = validate_data_type(data_type);
    if (!validation) {
        return base::fail(std::move(validation).error());
    }
    if (data_type.id() == kBoolTypeId) {
        return PreparedTypeInput{data_type, &parse_bool};
    }
    if (data_type.id() == kInt64TypeId) {
        return PreparedTypeInput{data_type, &parse_int64};
    }
    if (data_type.id() == kFloat32TypeId) {
        return PreparedTypeInput{data_type, &parse_float32};
    }
    if (data_type.id() == kCharTypeId) {
        return PreparedTypeInput{data_type, &parse_char};
    }
    if (data_type.id() == kTimestampTypeId) {
        return PreparedTypeInput{data_type, &parse_timestamp};
    }
    if (data_type.id() == kUnknownTypeId) {
        return PreparedTypeInput{data_type, &parse_unknown};
    }
    return base::fail(
        base::Error{
            base::ErrorCode::kInvariantViolation,
            "validated type has no text input function",
        }
    );
}

auto PreparedTypeInput::parse(const std::string_view text) const -> base::Result<Value> {
    return input_(data_type_, text);
}

auto PreparedTypeInput::parse_into(const std::string_view text, Value& destination) const
    -> base::Status {
    if (data_type_.id() == kCharTypeId) {
        const auto maximum = data_type_.modifier().is_none()
                                 ? text.size()
                                 : static_cast<std::size_t>(data_type_.modifier().value());
        const auto prefix = base::valid_utf8_prefix_size(text, maximum);
        if (!prefix.has_value()) {
            return base::fail(invalid_character_utf8());
        }
        if (destination.data_type_ == data_type_
            && std::holds_alternative<std::string>(destination.storage_)) {
            std::get<std::string>(destination.storage_).assign(text.substr(0U, prefix.value()));
            return {};
        }
        destination =
            Value::character(std::string{text.substr(0U, prefix.value())}, data_type_.modifier());
        return {};
    }
    auto parsed = input_(data_type_, text);
    if (!parsed) {
        return base::fail(std::move(parsed).error());
    }
    destination = std::move(parsed).value();
    return {};
}

auto PreparedTypeInput::null_value() const noexcept -> Value {
    return Value::null(data_type_);
}

auto PreparedTypeInput::data_type() const noexcept -> DataType {
    return data_type_;
}

auto input_value(const DataType data_type, const std::string_view text) -> base::Result<Value> {
    auto prepared = PreparedTypeInput::create(data_type);
    if (!prepared) {
        return base::fail(std::move(prepared).error());
    }
    return prepared->parse(text);
}

auto output_value(const Value& value) -> base::Result<std::string> {
    if (value.is_null()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "cannot output a NULL value through a type output function",
            }
        );
    }
    if (value.data_type().id() == kBoolTypeId && value.as_boolean() != nullptr) {
        return *value.as_boolean() ? std::string{"true"} : std::string{"false"};
    }
    if (value.data_type().id() == kInt64TypeId && value.as_integer() != nullptr) {
        return number_output(*value.as_integer());
    }
    if (value.data_type().id() == kFloat32TypeId && value.as_float() != nullptr) {
        return number_output(*value.as_float());
    }
    if (value.data_type().id() == kCharTypeId && value.as_string() != nullptr) {
        return *value.as_string();
    }
    if (value.data_type().id() == kTimestampTypeId && value.as_timestamp() != nullptr) {
        return format_timestamp(*value.as_timestamp());
    }
    return base::fail(
        base::Error{
            base::ErrorCode::kInvalidArgument,
            "value has no registered text output function",
        }
    );
}

auto apply_type_modifier(Value value, const DataType target) -> base::Result<Value> {
    auto validation = validate_data_type(target);
    if (!validation) {
        return base::fail(std::move(validation).error());
    }
    if (value.is_null()) {
        return Value::null(target);
    }
    if (value.data_type().id() != target.id() || target.id() == kUnknownTypeId) {
        return type_mismatch(value, target);
    }
    if (target.id() == kCharTypeId) {
        if (value.as_string() == nullptr) {
            return type_mismatch(value, target);
        }
        auto truncated = truncate_char(*value.as_string(), target.modifier());
        if (!truncated) {
            return base::fail(std::move(truncated).error());
        }
        return Value::character(std::move(truncated).value(), target.modifier());
    }
    if (target.id() == kTimestampTypeId) {
        if (value.as_timestamp() == nullptr) {
            return type_mismatch(value, target);
        }
        const auto precision = target.modifier().is_none()
                                   ? kMaxTimestampPrecision
                                   : static_cast<std::uint32_t>(target.modifier().value());
        auto adjusted = adjust_timestamp_precision(*value.as_timestamp(), precision);
        if (!adjusted) {
            return base::fail(std::move(adjusted).error());
        }
        return Value::timestamp(adjusted.value(), target.modifier());
    }
    return value;
}

}  // namespace db::types
