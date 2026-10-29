#include "db/base/error.hpp"

#include <exception>
#include <new>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "db/base/exception_boundary.hpp"

namespace db::base {

auto error_code_name(const ErrorCode code) noexcept -> std::string_view {
    switch (code) {
        case ErrorCode::kParse:
            return "parse";
        case ErrorCode::kBind:
            return "bind";
        case ErrorCode::kPlan:
            return "plan";
        case ErrorCode::kExecution:
            return "execution";
        case ErrorCode::kSerializationFailure:
            return "serialization_failure";
        case ErrorCode::kDeadlockDetected:
            return "deadlock_detected";
        case ErrorCode::kTransactionState:
            return "transaction_state";
        case ErrorCode::kInvalidArgument:
            return "invalid_argument";
        case ErrorCode::kOutOfRange:
            return "out_of_range";
        case ErrorCode::kNotFound:
            return "not_found";
        case ErrorCode::kAlreadyExists:
            return "already_exists";
        case ErrorCode::kUnsupported:
            return "unsupported";
        case ErrorCode::kIo:
            return "io";
        case ErrorCode::kCorruption:
            return "corruption";
        case ErrorCode::kInvariantViolation:
            return "invariant_violation";
        case ErrorCode::kResourceExhausted:
            return "resource_exhausted";
        case ErrorCode::kUnavailable:
            return "unavailable";
        case ErrorCode::kCancelled:
            return "cancelled";
        case ErrorCode::kInternal:
            return "internal";
        case ErrorCode::kRebuildRequired:
            return "rebuild_required";
    }

    return "unknown";
}

Error::Error(const ErrorCode code, std::string message)
    : code_{code},
      message_{std::move(message)} {}

Error::Error(const ErrorCode code, std::string message, std::error_code system_error)
    : code_{code},
      message_{std::move(message)},
      system_error_{system_error} {}

Error::Error(Error&& other) noexcept
    : code_{other.code_},
      message_{std::move(other.message_)},
      contexts_{std::move(other.contexts_)},
      system_error_{other.system_error_} {}

auto Error::operator=(Error&& other) noexcept -> Error& {
    if (this != &other) {
        code_ = other.code_;
        message_ = std::move(other.message_);
        contexts_ = std::move(other.contexts_);
        system_error_ = other.system_error_;
    }
    return *this;
}

auto Error::code() const noexcept -> ErrorCode {
    return code_;
}

auto Error::message() const noexcept -> std::string_view {
    return message_;
}

auto Error::contexts() const noexcept -> std::span<const ErrorContext> {
    return contexts_;
}

auto Error::system_error() const noexcept -> const std::error_code& {
    return system_error_;
}

auto Error::has_system_error() const noexcept -> bool {
    return static_cast<bool>(system_error_);
}

auto Error::is_retryable() const noexcept -> bool {
    return code_ == ErrorCode::kSerializationFailure || code_ == ErrorCode::kDeadlockDetected;
}

auto Error::with_context(std::string component, std::string operation) const& -> Error {
    Error copy = *this;
    copy.contexts_.push_back(
        ErrorContext{.component = std::move(component), .operation = std::move(operation)}
    );
    return copy;
}

auto Error::with_context(std::string component, std::string operation) && -> Error {
    contexts_.push_back(
        ErrorContext{.component = std::move(component), .operation = std::move(operation)}
    );
    return std::move(*this);
}

auto Error::to_string() const -> std::string {
    std::string description;

    for (const auto& context : contexts_ | std::views::reverse) {
        if (!context.component.empty()) {
            description.append(context.component);
            description.push_back('.');
        }
        description.append(context.operation);
        description.append(": ");
    }

    description.append(message_);
    if (has_system_error()) {
        description.append(": ");
        description.append(system_error_.message());
    }
    return description;
}

namespace detail {

auto current_exception_boundary_error(
    const ExceptionBoundaryPolicy& policy,
    const std::string_view operation
) -> Error {
    const auto boundary_error = [&policy,
                                 operation](const ErrorCode code, std::string message) -> Error {
        Error error{code, std::move(message)};
        if (policy.component.empty() && operation.empty()) {
            return error;
        }
        return std::move(error).with_context(std::string{policy.component}, std::string{operation});
    };
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return boundary_error(
            ErrorCode::kResourceExhausted,
            std::string{policy.allocation_message}
        );
    } catch (const std::length_error&) {
        return boundary_error(
            ErrorCode::kResourceExhausted,
            std::string{policy.allocation_message}
        );
    } catch (const std::exception& exception) {
        std::string message{policy.exception_prefix};
        message.append(exception.what());
        return boundary_error(ErrorCode::kInternal, std::move(message));
    } catch (...) {
        return boundary_error(ErrorCode::kInternal, std::string{policy.unknown_exception_message});
    }
}

}  // namespace detail

}  // namespace db::base
