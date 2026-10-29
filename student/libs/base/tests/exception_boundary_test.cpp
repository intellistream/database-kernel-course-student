#include <new>
#include <stdexcept>
#include <string_view>

#include <gtest/gtest.h>

#include "db/base/exception_boundary.hpp"

namespace db::base {
namespace {

inline constexpr ExceptionBoundaryPolicy kTestPolicy{
    .component = "base.boundary_test",
    .allocation_message = "test allocation failed",
    .exception_prefix = "test exception: ",
    .unknown_exception_message = "test unknown exception",
};

TEST(ExceptionBoundaryTest, PreservesReturnedValuesAndDbFailures) {
    auto value = invoke_exception_boundary(kTestPolicy, "return value", []() -> Result<int> {
        return 42;
    });
    ASSERT_TRUE(value);
    EXPECT_EQ(value.value(), 42);

    auto failure = invoke_exception_boundary(kTestPolicy, "return failure", []() -> Result<int> {
        return fail(Error{ErrorCode::kNotFound, "missing"});
    });
    ASSERT_FALSE(failure);
    EXPECT_EQ(failure.error().code(), ErrorCode::kNotFound);
    EXPECT_EQ(failure.error().message(), std::string_view{"missing"});
    EXPECT_TRUE(failure.error().contexts().empty());
}

TEST(ExceptionBoundaryTest, ClassifiesAllocationExceptionsAsResourceExhaustion) {
    auto allocation = invoke_exception_boundary(kTestPolicy, "allocate", []() -> Result<int> {
        throw std::bad_alloc{};
    });
    ASSERT_FALSE(allocation);
    EXPECT_EQ(allocation.error().code(), ErrorCode::kResourceExhausted);
    EXPECT_EQ(allocation.error().message(), std::string_view{"test allocation failed"});
    ASSERT_EQ(allocation.error().contexts().size(), 1U);
    EXPECT_EQ(allocation.error().contexts().front().component, "base.boundary_test");
    EXPECT_EQ(allocation.error().contexts().front().operation, "allocate");

    auto oversized = invoke_exception_boundary(kTestPolicy, "grow", []() -> Status {
        throw std::length_error{"oversized"};
    });
    ASSERT_FALSE(oversized);
    EXPECT_EQ(oversized.error().code(), ErrorCode::kResourceExhausted);
}

TEST(ExceptionBoundaryTest, ClassifiesUnexpectedExceptionsAsInternal) {
    auto standard = invoke_exception_boundary(kTestPolicy, "invoke callback", []() -> Status {
        throw std::runtime_error{"broken"};
    });
    ASSERT_FALSE(standard);
    EXPECT_EQ(standard.error().code(), ErrorCode::kInternal);
    EXPECT_EQ(standard.error().message(), std::string_view{"test exception: broken"});

    auto unknown = invoke_exception_boundary(kTestPolicy, "invoke callback", []() -> Status {
        throw 7;
    });
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code(), ErrorCode::kInternal);
    EXPECT_EQ(unknown.error().message(), std::string_view{"test unknown exception"});
}

TEST(ExceptionBoundaryTest, SupportsContextFreePortConversion) {
    constexpr ExceptionBoundaryPolicy policy{
        .component = {},
        .allocation_message = "port allocation failed",
        .exception_prefix = "port exception: ",
        .unknown_exception_message = "unknown port exception",
    };
    auto result = invoke_exception_boundary(policy, []() -> Status {
        throw std::runtime_error{"broken"};
    });
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().message(), std::string_view{"port exception: broken"});
    EXPECT_TRUE(result.error().contexts().empty());
}

}  // namespace
}  // namespace db::base
