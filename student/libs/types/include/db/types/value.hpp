#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>

#include "db/base/result.hpp"
#include "db/types/data_type.hpp"

namespace db::types {

class PreparedTypeInput;

class TimestampValue final {
public:
    constexpr explicit TimestampValue(const std::int64_t microseconds) noexcept
        : microseconds_{microseconds} {}

    constexpr auto microseconds() const noexcept -> std::int64_t {
        return microseconds_;
    }

    constexpr auto operator==(const TimestampValue& other) const noexcept -> bool {
        return microseconds_ == other.microseconds_;
    }

    constexpr auto operator<=>(const TimestampValue& other) const noexcept {
        return microseconds_ <=> other.microseconds_;
    }

private:
    std::int64_t microseconds_;
};

enum class ValueKind : std::uint8_t {
    kNull,
    kBoolean,
    kInteger,
    kFloat,
    kString,
    kTimestamp,
};

enum class SqlTruth : std::uint8_t {
    kFalse,
    kTrue,
    kUnknown,
};

constexpr auto sql_not(const SqlTruth value) noexcept -> SqlTruth {
    switch (value) {
        case SqlTruth::kFalse:
            return SqlTruth::kTrue;
        case SqlTruth::kTrue:
            return SqlTruth::kFalse;
        case SqlTruth::kUnknown:
            return SqlTruth::kUnknown;
    }
    return SqlTruth::kUnknown;
}

constexpr auto sql_and(const SqlTruth left, const SqlTruth right) noexcept -> SqlTruth {
    if (left == SqlTruth::kFalse || right == SqlTruth::kFalse) {
        return SqlTruth::kFalse;
    }
    if (left == SqlTruth::kUnknown || right == SqlTruth::kUnknown) {
        return SqlTruth::kUnknown;
    }
    return SqlTruth::kTrue;
}

constexpr auto sql_or(const SqlTruth left, const SqlTruth right) noexcept -> SqlTruth {
    if (left == SqlTruth::kTrue || right == SqlTruth::kTrue) {
        return SqlTruth::kTrue;
    }
    if (left == SqlTruth::kUnknown || right == SqlTruth::kUnknown) {
        return SqlTruth::kUnknown;
    }
    return SqlTruth::kFalse;
}

class Value final {
public:
    static auto null(DataType data_type) -> Value;
    static auto boolean(bool value) -> Value;
    static auto integer(std::int64_t value) -> Value;
    static auto floating(float value) -> Value;
    static auto character(std::string value, TypeModifier modifier = kNoTypeModifier) -> Value;
    static auto unknown_string(std::string value) -> Value;
    static auto timestamp(TimestampValue value, TypeModifier modifier = kNoTypeModifier) -> Value;

    auto data_type() const noexcept -> DataType;
    auto kind() const noexcept -> ValueKind;
    auto is_null() const noexcept -> bool;

    auto as_boolean() const noexcept -> const bool*;
    auto as_integer() const noexcept -> const std::int64_t*;
    auto as_float() const noexcept -> const float*;
    auto as_string() const noexcept -> const std::string*;
    auto as_timestamp() const noexcept -> const TimestampValue*;

    auto display() const -> std::string;
    auto hash() const noexcept -> std::size_t;

    auto operator==(const Value& other) const noexcept -> bool;

private:
    using Storage =
        std::variant<std::monostate, bool, std::int64_t, float, std::string, TimestampValue>;

    Value(DataType data_type, Storage storage);

    friend auto sql_compare(const Value& left, const Value& right)
        -> base::Result<std::partial_ordering>;
    friend class PreparedTypeInput;

    DataType data_type_;
    Storage storage_;
};

auto value_kind_order(const Value& left, const Value& right) noexcept -> std::strong_ordering;
auto sql_compare(const Value& left, const Value& right) -> base::Result<std::partial_ordering>;
auto sql_equal(const Value& left, const Value& right) -> base::Result<SqlTruth>;

}  // namespace db::types

template <>
struct std::hash<db::types::TimestampValue> {
    auto operator()(const db::types::TimestampValue value) const noexcept -> std::size_t {
        return std::hash<std::int64_t>{}(value.microseconds());
    }
};

template <>
struct std::hash<db::types::Value> {
    auto operator()(const db::types::Value& value) const noexcept -> std::size_t {
        return value.hash();
    }
};
