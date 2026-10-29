#include <array>
#include <cerrno>
#include <concepts>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/result.hpp"

namespace db::base {
namespace {

TEST(ErrorTest, PreservesStructuredFields) {
    const Error error{ErrorCode::kCorruption, "invalid page header"};

    EXPECT_EQ(error.code(), ErrorCode::kCorruption);
    EXPECT_EQ(error.message(), "invalid page header");
    EXPECT_TRUE(error.contexts().empty());
    EXPECT_FALSE(error.has_system_error());
    EXPECT_EQ(error.to_string(), "invalid page header");
}

TEST(ErrorTest, RendersContextsFromOuterToInner) {
    const Error original{ErrorCode::kIo, "write failed"};
    const Error contextual =
        original.with_context("storage", "flush page").with_context("engine", "checkpoint");

    ASSERT_EQ(contextual.contexts().size(), 2U);
    EXPECT_EQ(
        contextual.contexts()[0],
        (ErrorContext{.component = "storage", .operation = "flush page"})
    );
    EXPECT_EQ(
        contextual.contexts()[1],
        (ErrorContext{.component = "engine", .operation = "checkpoint"})
    );
    EXPECT_TRUE(original.contexts().empty());
    EXPECT_EQ(contextual.to_string(), "engine.checkpoint: storage.flush page: write failed");
}

TEST(ErrorTest, RendersOperationWithoutSeparatorForEmptyComponent) {
    const Error error = Error{ErrorCode::kIo, "write failed"}.with_context("", "flush page");

    ASSERT_EQ(error.contexts().size(), 1U);
    EXPECT_EQ(error.contexts()[0].component, "");
    EXPECT_EQ(error.to_string(), "flush page: write failed");
}

TEST(ErrorTest, PreservesSystemErrorCause) {
    const std::error_code cause{ENOENT, std::generic_category()};
    const Error error{ErrorCode::kIo, "open catalog", cause};

    EXPECT_TRUE(error.has_system_error());
    EXPECT_EQ(error.system_error(), cause);
    EXPECT_EQ(error.to_string(), "open catalog: " + cause.message());
}

TEST(ErrorTest, MarksOnlyFrontendRetryCategoriesRetryable) {
    const Error serialization{ErrorCode::kSerializationFailure, "serialization conflict"};
    const Error deadlock{ErrorCode::kDeadlockDetected, "deadlock"};
    const Error transaction_state{ErrorCode::kTransactionState, "aborted"};
    const Error unavailable{ErrorCode::kUnavailable, "temporarily unavailable"};

    EXPECT_TRUE(serialization.is_retryable());
    EXPECT_TRUE(deadlock.is_retryable());
    EXPECT_FALSE(transaction_state.is_retryable());
    EXPECT_FALSE(unavailable.is_retryable());
}

TEST(ErrorTest, ExposesStableCodeNames) {
    constexpr std::array names{
        std::pair{ErrorCode::kParse, std::string_view{"parse"}},
        std::pair{ErrorCode::kBind, std::string_view{"bind"}},
        std::pair{ErrorCode::kPlan, std::string_view{"plan"}},
        std::pair{ErrorCode::kExecution, std::string_view{"execution"}},
        std::pair{ErrorCode::kSerializationFailure, std::string_view{"serialization_failure"}},
        std::pair{ErrorCode::kDeadlockDetected, std::string_view{"deadlock_detected"}},
        std::pair{ErrorCode::kTransactionState, std::string_view{"transaction_state"}},
        std::pair{ErrorCode::kInvalidArgument, std::string_view{"invalid_argument"}},
        std::pair{ErrorCode::kOutOfRange, std::string_view{"out_of_range"}},
        std::pair{ErrorCode::kNotFound, std::string_view{"not_found"}},
        std::pair{ErrorCode::kAlreadyExists, std::string_view{"already_exists"}},
        std::pair{ErrorCode::kUnsupported, std::string_view{"unsupported"}},
        std::pair{ErrorCode::kIo, std::string_view{"io"}},
        std::pair{ErrorCode::kCorruption, std::string_view{"corruption"}},
        std::pair{ErrorCode::kInvariantViolation, std::string_view{"invariant_violation"}},
        std::pair{ErrorCode::kResourceExhausted, std::string_view{"resource_exhausted"}},
        std::pair{ErrorCode::kUnavailable, std::string_view{"unavailable"}},
        std::pair{ErrorCode::kCancelled, std::string_view{"cancelled"}},
        std::pair{ErrorCode::kInternal, std::string_view{"internal"}},
        std::pair{ErrorCode::kRebuildRequired, std::string_view{"rebuild_required"}},
    };

    for (const auto& [code, name] : names) {
        EXPECT_EQ(error_code_name(code), name);
    }
    // ErrorCode has a fixed uint8_t underlying type, so this value is representable and
    // deliberately exercises the stable fallback for a future/unknown wire value.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_EQ(error_code_name(static_cast<ErrorCode>(255)), "unknown");
}

TEST(ResultTest, CarriesValuesAndStructuredErrors) {
    const Result<int> value{42};
    const Result<int> error = fail(Error{ErrorCode::kInvalidArgument, "negative capacity"});
    const Status ok{};
    const Status failed = fail(Error{ErrorCode::kCancelled, "shutdown"});

    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, 42);
    ASSERT_FALSE(error.has_value());
    EXPECT_EQ(error.error().code(), ErrorCode::kInvalidArgument);
    EXPECT_TRUE(ok.has_value());
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().message(), "shutdown");
}

TEST(ResultTest, SupportsMoveOnlyValues) {
    Result<std::unique_ptr<int>> result{std::make_unique<int>(42)};

    ASSERT_TRUE(result);
    ASSERT_NE(*result, nullptr);
    EXPECT_EQ(**result, 42);

    const std::unique_ptr<int> value = std::move(result).value();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 42);
}

TEST(ResultTest, CopiesValuesFromLvalues) {
    const std::string source{"catalog"};
    const Result<std::string> result{source};

    ASSERT_TRUE(result);
    EXPECT_EQ(*result, source);
}

TEST(ResultTest, ConvertsToBoolOnlyExplicitly) {
    static_assert(!std::is_convertible_v<Result<int>, bool>);

    const Result<int> value{42};
    const Result<int> error = fail(Error{ErrorCode::kInvalidArgument, "invalid value"});

    EXPECT_TRUE(static_cast<bool>(value));
    EXPECT_FALSE(static_cast<bool>(error));
}

TEST(ResultTest, MovesValuesAndErrorsFromRvalues) {
    static_assert(std::same_as<decltype(std::declval<Result<int>&&>().value()), int&&>);
    static_assert(std::same_as<decltype(std::declval<const Result<int>&&>().value()), const int&&>);
    static_assert(std::same_as<decltype(std::declval<Result<int>&&>().error()), Error&&>);
    static_assert(
        std::same_as<decltype(std::declval<const Result<int>&&>().error()), const Error&&>
    );
    static_assert(std::same_as<decltype(std::declval<Status&&>().error()), Error&&>);
    static_assert(std::same_as<decltype(std::declval<const Status&&>().error()), const Error&&>);

    Result<std::string> value{std::string{"catalog"}};
    Result<int> failure = fail(Error{ErrorCode::kNotFound, "missing catalog"});
    Status failed_status = fail(Error{ErrorCode::kCancelled, "shutdown"});
    const Status const_failed_status = fail(Error{ErrorCode::kUnavailable, "offline"});

    EXPECT_EQ(std::move(value).value(), "catalog");
    EXPECT_EQ(std::move(failure).error().code(), ErrorCode::kNotFound);
    EXPECT_EQ(std::move(failed_status).error().message(), "shutdown");
    EXPECT_EQ(static_cast<const Status&&>(const_failed_status).error().message(), "offline");
}

TEST(ResultTest, ProvidesPointerLikeAccess) {
    struct Record final {
        std::string name;

        auto size() const noexcept -> std::size_t {
            return name.size();
        }
    };

    Result<Record> record{Record{.name = "catalog"}};
    const Result<Record> const_record{Record{.name = "page"}};

    EXPECT_EQ(record->name, "catalog");
    EXPECT_EQ(record->size(), 7U);
    EXPECT_EQ(const_record->name, "page");
    EXPECT_EQ(const_record->size(), 4U);
}

}  // namespace
}  // namespace db::base
