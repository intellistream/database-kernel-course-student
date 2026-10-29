#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace db::base {

// ErrorCode is not persisted. New codes may be appended as subsystem contracts
// are implemented, while existing meanings remain stable for API mapping.
enum class ErrorCode : std::uint8_t {
    kParse,
    kBind,
    kPlan,
    kExecution,
    kSerializationFailure,
    kDeadlockDetected,
    kTransactionState,
    kInvalidArgument,
    kOutOfRange,
    kNotFound,
    kAlreadyExists,
    kUnsupported,
    kIo,
    kCorruption,
    kInvariantViolation,
    kResourceExhausted,
    kUnavailable,
    kCancelled,
    kInternal,
    kRebuildRequired,
};

auto error_code_name(ErrorCode code) noexcept -> std::string_view;

struct ErrorContext final {
    std::string component;
    std::string operation;

    auto operator==(const ErrorContext&) const -> bool = default;
};

class Error final {
public:
    Error(ErrorCode code, std::string message);
    Error(ErrorCode code, std::string message, std::error_code system_error);
    Error(const Error&) = default;
    Error(Error&& other) noexcept;
    auto operator=(const Error&) -> Error& = default;
    auto operator=(Error&& other) noexcept -> Error&;
    ~Error() = default;

    auto code() const noexcept -> ErrorCode;
    auto message() const noexcept -> std::string_view;
    auto contexts() const noexcept -> std::span<const ErrorContext>;
    auto system_error() const noexcept -> const std::error_code&;
    auto has_system_error() const noexcept -> bool;
    auto is_retryable() const noexcept -> bool;

    // Contexts are added from inner to outer layers. Rendering reverses them so
    // the highest-level operation appears first.
    auto with_context(std::string component, std::string operation) const& -> Error;
    auto with_context(std::string component, std::string operation) && -> Error;

    auto to_string() const -> std::string;

    auto operator==(const Error&) const -> bool = default;

private:
    ErrorCode code_{ErrorCode::kInternal};
    std::string message_;
    std::vector<ErrorContext> contexts_;
    std::error_code system_error_;
};

}  // namespace db::base
