#include "db/types/value.hpp"

#include <array>
#include <bit>
#include <charconv>
#include <exception>
#include <limits>
#include <string_view>
#include <system_error>

#include "db/base/error.hpp"
#include "db/base/hash.hpp"
#include "db/types/datetime.hpp"

namespace db::types {
namespace {

template <typename ValueType, typename Variant>
auto get_invariant(const Variant& storage) noexcept -> const ValueType& {
    const auto* const value = std::get_if<ValueType>(&storage);
    if (value == nullptr) {
        std::terminate();
    }
    return *value;
}

template <typename Number>
auto number_display(const Number value) -> std::string {
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.begin(), buffer.end(), value);
    if (result.ec != std::errc{}) {
        return "<numeric-display-error>";
    }
    return std::string{buffer.begin(), result.ptr};
}

auto string_display(const std::string_view value) -> std::string {
    std::string output;
    output.reserve(value.size() + 2U);
    output.push_back('\'');
    for (const char character : value) {
        output.push_back(character);
        if (character == '\'') {
            output.push_back('\'');
        }
    }
    output.push_back('\'');
    return output;
}

auto incompatible_comparison(const Value& left, const Value& right) -> base::Error {
    return {
        base::ErrorCode::kInvalidArgument,
        "SQL comparison requires values with the same concrete base type (left="
            + number_display(left.data_type().id().value())
            + ", right=" + number_display(right.data_type().id().value()) + ")",
    };
}

constexpr auto as_partial(const std::strong_ordering ordering) noexcept -> std::partial_ordering {
    if (ordering == std::strong_ordering::less) {
        return std::partial_ordering::less;
    }
    if (ordering == std::strong_ordering::greater) {
        return std::partial_ordering::greater;
    }
    return std::partial_ordering::equivalent;
}

}  // namespace

auto Value::null(const DataType data_type) -> Value {
    return {data_type, std::monostate{}};
}

auto Value::boolean(const bool value) -> Value {
    return {DataType{kBoolTypeId}, value};
}

auto Value::integer(const std::int64_t value) -> Value {
    return {DataType{kInt64TypeId}, value};
}

auto Value::floating(const float value) -> Value {
    return {DataType{kFloat32TypeId}, value};
}

auto Value::character(std::string value, const TypeModifier modifier) -> Value {
    return {DataType{kCharTypeId, modifier}, std::move(value)};
}

auto Value::unknown_string(std::string value) -> Value {
    return {DataType{kUnknownTypeId}, std::move(value)};
}

auto Value::timestamp(const TimestampValue value, const TypeModifier modifier) -> Value {
    return {DataType{kTimestampTypeId, modifier}, value};
}

auto Value::data_type() const noexcept -> DataType {
    return data_type_;
}

auto Value::kind() const noexcept -> ValueKind {
    return static_cast<ValueKind>(storage_.index());
}

auto Value::is_null() const noexcept -> bool {
    return std::holds_alternative<std::monostate>(storage_);
}

auto Value::as_boolean() const noexcept -> const bool* {
    return std::get_if<bool>(&storage_);
}

auto Value::as_integer() const noexcept -> const std::int64_t* {
    return std::get_if<std::int64_t>(&storage_);
}

auto Value::as_float() const noexcept -> const float* {
    return std::get_if<float>(&storage_);
}

auto Value::as_string() const noexcept -> const std::string* {
    return std::get_if<std::string>(&storage_);
}

auto Value::as_timestamp() const noexcept -> const TimestampValue* {
    return std::get_if<TimestampValue>(&storage_);
}

auto Value::display() const -> std::string {
    switch (kind()) {
        case ValueKind::kNull:
            return "NULL";
        case ValueKind::kBoolean:
            return get_invariant<bool>(storage_) ? "true" : "false";
        case ValueKind::kInteger:
            return number_display(get_invariant<std::int64_t>(storage_));
        case ValueKind::kFloat:
            return number_display(get_invariant<float>(storage_));
        case ValueKind::kString:
            return string_display(get_invariant<std::string>(storage_));
        case ValueKind::kTimestamp:
            return format_timestamp(get_invariant<TimestampValue>(storage_));
    }
    return "<invalid-value>";
}

auto Value::hash() const noexcept -> std::size_t {
    auto result = std::hash<DataType>{}(data_type_);
    result = base::combine_hash(result, std::hash<std::size_t>{}(storage_.index()));
    switch (kind()) {
        case ValueKind::kNull:
            return result;
        case ValueKind::kBoolean:
            return base::combine_hash(result, std::hash<bool>{}(get_invariant<bool>(storage_)));
        case ValueKind::kInteger:
            return base::combine_hash(
                result,
                std::hash<std::int64_t>{}(get_invariant<std::int64_t>(storage_))
            );
        case ValueKind::kFloat:
            return base::combine_hash(
                result,
                std::hash<std::uint32_t>{}(
                    std::bit_cast<std::uint32_t>(get_invariant<float>(storage_))
                )
            );
        case ValueKind::kString:
            return base::combine_hash(
                result,
                std::hash<std::string>{}(get_invariant<std::string>(storage_))
            );
        case ValueKind::kTimestamp:
            return base::combine_hash(
                result,
                std::hash<TimestampValue>{}(get_invariant<TimestampValue>(storage_))
            );
    }
    return result;
}

auto Value::operator==(const Value& other) const noexcept -> bool {
    if (data_type_ != other.data_type_ || kind() != other.kind()) {
        return false;
    }
    switch (kind()) {
        case ValueKind::kNull:
            return true;
        case ValueKind::kBoolean:
            return get_invariant<bool>(storage_) == get_invariant<bool>(other.storage_);
        case ValueKind::kInteger:
            return get_invariant<std::int64_t>(storage_)
                   == get_invariant<std::int64_t>(other.storage_);
        case ValueKind::kFloat:
            return std::bit_cast<std::uint32_t>(get_invariant<float>(storage_))
                   == std::bit_cast<std::uint32_t>(get_invariant<float>(other.storage_));
        case ValueKind::kString:
            return get_invariant<std::string>(storage_)
                   == get_invariant<std::string>(other.storage_);
        case ValueKind::kTimestamp:
            return get_invariant<TimestampValue>(storage_)
                   == get_invariant<TimestampValue>(other.storage_);
    }
    return false;
}

Value::Value(const DataType data_type, Storage storage)
    : data_type_{data_type},
      storage_{std::move(storage)} {}

auto value_kind_order(const Value& left, const Value& right) noexcept -> std::strong_ordering {
    return static_cast<std::uint8_t>(left.kind()) <=> static_cast<std::uint8_t>(right.kind());
}

auto sql_compare(const Value& left, const Value& right) -> base::Result<std::partial_ordering> {
    if (left.is_null() || right.is_null()) {
        return std::partial_ordering::unordered;
    }
    if (left.data_type().id() != right.data_type().id() || left.data_type().id() == kUnknownTypeId
        || left.kind() != right.kind()) {
        return base::fail(incompatible_comparison(left, right));
    }

    switch (left.kind()) {
        case ValueKind::kNull:
            return std::partial_ordering::unordered;
        case ValueKind::kBoolean: {
            const bool left_value = get_invariant<bool>(left.storage_);
            const bool right_value = get_invariant<bool>(right.storage_);
            if (left_value == right_value) {
                return std::partial_ordering::equivalent;
            }
            return left_value ? std::partial_ordering::greater : std::partial_ordering::less;
        }
        case ValueKind::kInteger:
            return as_partial(
                get_invariant<std::int64_t>(left.storage_)
                <=> get_invariant<std::int64_t>(right.storage_)
            );
        case ValueKind::kFloat:
            return get_invariant<float>(left.storage_) <=> get_invariant<float>(right.storage_);
        case ValueKind::kString:
            return as_partial(
                get_invariant<std::string>(left.storage_)
                <=> get_invariant<std::string>(right.storage_)
            );
        case ValueKind::kTimestamp:
            return as_partial(
                get_invariant<TimestampValue>(left.storage_)
                <=> get_invariant<TimestampValue>(right.storage_)
            );
    }
    return base::fail(
        base::Error{
            base::ErrorCode::kInvariantViolation,
            "unrecognized value kind",
        }
    );
}

auto sql_equal(const Value& left, const Value& right) -> base::Result<SqlTruth> {
    auto comparison = sql_compare(left, right);
    if (!comparison) {
        return base::fail(std::move(comparison).error());
    }
    if (comparison.value() == std::partial_ordering::unordered) {
        return SqlTruth::kUnknown;
    }
    return comparison.value() == std::partial_ordering::equivalent ? SqlTruth::kTrue
                                                                   : SqlTruth::kFalse;
}

}  // namespace db::types
