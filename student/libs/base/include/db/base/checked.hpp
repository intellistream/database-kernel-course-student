#pragma once

#include <concepts>
#include <limits>
#include <type_traits>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/result.hpp"

namespace db::base {

template <typename Value>
concept ArithmeticInteger = std::integral<Value> && !std::same_as<std::remove_cv_t<Value>, bool>;

template <ArithmeticInteger Value>
auto checked_add(const Value left, const Value right) -> Result<Value> {
    constexpr Value minimum = std::numeric_limits<Value>::min();
    constexpr Value maximum = std::numeric_limits<Value>::max();

    if constexpr (std::is_signed_v<Value>) {
        if ((right > 0 && left > maximum - right) || (right < 0 && left < minimum - right)) {
            return fail(Error{ErrorCode::kOutOfRange, "integer addition overflow"});
        }
    } else if (left > maximum - right) {
        return fail(Error{ErrorCode::kOutOfRange, "integer addition overflow"});
    }
    return static_cast<Value>(left + right);
}

template <ArithmeticInteger Value>
auto checked_subtract(const Value left, const Value right) -> Result<Value> {
    constexpr Value minimum = std::numeric_limits<Value>::min();
    constexpr Value maximum = std::numeric_limits<Value>::max();

    if constexpr (std::is_signed_v<Value>) {
        if ((right > 0 && left < minimum + right) || (right < 0 && left > maximum + right)) {
            return fail(Error{ErrorCode::kOutOfRange, "integer subtraction overflow"});
        }
    } else if (left < right) {
        return fail(Error{ErrorCode::kOutOfRange, "integer subtraction overflow"});
    }
    return static_cast<Value>(left - right);
}

template <ArithmeticInteger Value>
auto checked_multiply(const Value left, const Value right) -> Result<Value> {
    constexpr Value minimum = std::numeric_limits<Value>::min();
    constexpr Value maximum = std::numeric_limits<Value>::max();

    bool overflow = false;
    if constexpr (std::is_signed_v<Value>) {
        if (left > 0) {
            overflow = right > 0 ? left > maximum / right : right < minimum / left;
        } else if (left < 0) {
            overflow = right > 0 ? left < minimum / right : right < maximum / left;
        }
    } else {
        overflow = right != 0 && left > maximum / right;
    }

    if (overflow) {
        return fail(Error{ErrorCode::kOutOfRange, "integer multiplication overflow"});
    }
    return static_cast<Value>(left * right);
}

template <ArithmeticInteger Target, ArithmeticInteger Source>
auto checked_cast(const Source value) -> Result<Target> {
    if (!std::in_range<Target>(value)) {
        return fail(Error{ErrorCode::kOutOfRange, "integer conversion out of range"});
    }
    return static_cast<Target>(value);
}

}  // namespace db::base
