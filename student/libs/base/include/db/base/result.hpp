#pragma once

#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "db/base/error.hpp"

namespace db::base {

namespace detail {

struct Failure final {
    Error error;
};

}  // namespace detail

// DB keeps this project-owned result contract under C++23 so structured
// Error semantics remain stable across component boundaries and move-only values.
template <typename Value>
class Result final {
    static_assert(!std::is_void_v<Value>);

public:
    Result()
        requires std::default_initializable<Value>
        : Result{Value{}} {}

    Result(const Value& value)
        requires std::copy_constructible<Value>
        : storage_{std::in_place_type<StoredValue>, value} {}

    Result(Value&& value)
        : storage_{std::in_place_type<StoredValue>, std::move(value)} {}

    Result(detail::Failure failure)
        : storage_{std::in_place_type<detail::Failure>, std::move(failure)} {}

    auto has_value() const noexcept -> bool {
        return std::holds_alternative<StoredValue>(storage_);
    }

    explicit operator bool() const noexcept {
        return has_value();
    }

    auto value() & -> Value& {
        return std::get<StoredValue>(storage_).value;
    }

    auto value() const& -> const Value& {
        return std::get<StoredValue>(storage_).value;
    }

    auto value() && -> Value&& {
        return std::move(std::get<StoredValue>(storage_).value);
    }

    auto value() const&& -> const Value&& {
        return std::move(std::get<StoredValue>(storage_).value);
    }

    auto operator*() & -> Value& {
        return value();
    }

    auto operator*() const& -> const Value& {
        return value();
    }

    auto operator*() && -> Value&& {
        return std::move(*this).value();
    }

    auto operator*() const&& -> const Value&& {
        return std::move(*this).value();
    }

    auto operator->() -> Value* {
        return std::addressof(value());
    }

    auto operator->() const -> const Value* {
        return std::addressof(value());
    }

    auto error() & -> Error& {
        return std::get<detail::Failure>(storage_).error;
    }

    auto error() const& -> const Error& {
        return std::get<detail::Failure>(storage_).error;
    }

    auto error() && -> Error&& {
        return std::move(std::get<detail::Failure>(storage_).error);
    }

    auto error() const&& -> const Error&& {
        return std::move(std::get<detail::Failure>(storage_).error);
    }

private:
    struct StoredValue final {
        explicit StoredValue(const Value& stored_value)
            requires std::copy_constructible<Value>
            : value{stored_value} {}

        explicit StoredValue(Value&& stored_value)
            : value{std::move(stored_value)} {}

        Value value;
    };

    std::variant<StoredValue, detail::Failure> storage_;
};

template <>
class Result<void> final {
public:
    Result() = default;

    Result(detail::Failure failure)
        : storage_{std::in_place_type<Error>, std::move(failure.error)} {}

    auto has_value() const noexcept -> bool {
        return std::holds_alternative<std::monostate>(storage_);
    }

    explicit operator bool() const noexcept {
        return has_value();
    }

    auto error() & -> Error& {
        return std::get<Error>(storage_);
    }

    auto error() const& -> const Error& {
        return std::get<Error>(storage_);
    }

    auto error() && -> Error&& {
        return std::move(std::get<Error>(storage_));
    }

    auto error() const&& -> const Error&& {
        return std::move(std::get<Error>(storage_));
    }

private:
    std::variant<std::monostate, Error> storage_;
};

using Status = Result<void>;

inline auto fail(Error error) -> detail::Failure {
    return detail::Failure{.error = std::move(error)};
}

}  // namespace db::base
