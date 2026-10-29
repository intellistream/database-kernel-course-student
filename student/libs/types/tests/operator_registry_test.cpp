#include <array>

#include <gtest/gtest.h>

#include "db/types/operator_registry.hpp"

namespace db::types {
namespace {

TEST(OperatorRegistryTest, ResolvesTheCompleteDocumentedOperatorSurface) {
    constexpr std::array logical{BinaryOperator::kAnd, BinaryOperator::kOr};
    constexpr std::array integer_arithmetic{
        BinaryOperator::kAdd,
        BinaryOperator::kSubtract,
        BinaryOperator::kMultiply,
        BinaryOperator::kDivide,
        BinaryOperator::kModulo,
    };
    constexpr std::array float_arithmetic{
        BinaryOperator::kAdd,
        BinaryOperator::kSubtract,
        BinaryOperator::kMultiply,
        BinaryOperator::kDivide,
    };
    constexpr std::array comparisons{
        BinaryOperator::kEqual,
        BinaryOperator::kNotEqual,
        BinaryOperator::kLess,
        BinaryOperator::kLessEqual,
        BinaryOperator::kGreater,
        BinaryOperator::kGreaterEqual,
    };
    constexpr std::array comparable_types{
        kBoolTypeId,
        kInt64TypeId,
        kFloat32TypeId,
        kCharTypeId,
        kTimestampTypeId,
    };

    for (const auto operation : logical) {
        EXPECT_TRUE(resolve_binary_operator(operation, kBoolTypeId, kBoolTypeId));
    }
    for (const auto operation : integer_arithmetic) {
        EXPECT_TRUE(resolve_binary_operator(operation, kInt64TypeId, kInt64TypeId));
    }
    for (const auto operation : float_arithmetic) {
        EXPECT_TRUE(resolve_binary_operator(operation, kFloat32TypeId, kFloat32TypeId));
    }
    for (const auto operation : comparisons) {
        for (const auto type : comparable_types) {
            const auto resolved = resolve_binary_operator(operation, type, type);
            ASSERT_TRUE(resolved);
            EXPECT_EQ(resolved.value(), kBoolTypeId);
        }
    }
}

TEST(OperatorRegistryTest, ResolvesLogicalAndNumericFamilies) {
    const auto logical = resolve_binary_operator(BinaryOperator::kAnd, kBoolTypeId, kBoolTypeId);
    const auto integer_modulo =
        resolve_binary_operator(BinaryOperator::kModulo, kInt64TypeId, kInt64TypeId);
    const auto float_add =
        resolve_binary_operator(BinaryOperator::kAdd, kFloat32TypeId, kFloat32TypeId);

    ASSERT_TRUE(logical);
    EXPECT_EQ(logical.value(), kBoolTypeId);
    ASSERT_TRUE(integer_modulo);
    EXPECT_EQ(integer_modulo.value(), kInt64TypeId);
    ASSERT_TRUE(float_add);
    EXPECT_EQ(float_add.value(), kFloat32TypeId);
}

TEST(OperatorRegistryTest, RegistersComparisonsForAllComparableBuiltins) {
    EXPECT_TRUE(resolve_binary_operator(BinaryOperator::kEqual, kBoolTypeId, kBoolTypeId));
    EXPECT_TRUE(resolve_binary_operator(BinaryOperator::kLess, kInt64TypeId, kInt64TypeId));
    EXPECT_TRUE(
        resolve_binary_operator(BinaryOperator::kGreaterEqual, kFloat32TypeId, kFloat32TypeId)
    );
    EXPECT_TRUE(resolve_binary_operator(BinaryOperator::kNotEqual, kCharTypeId, kCharTypeId));
    EXPECT_TRUE(
        resolve_binary_operator(BinaryOperator::kLessEqual, kTimestampTypeId, kTimestampTypeId)
    );
}

TEST(OperatorRegistryTest, RejectsMixedAndUnsupportedSignatures) {
    EXPECT_FALSE(resolve_binary_operator(BinaryOperator::kAdd, kInt64TypeId, kFloat32TypeId));
    EXPECT_FALSE(resolve_binary_operator(BinaryOperator::kModulo, kFloat32TypeId, kFloat32TypeId));
    EXPECT_FALSE(resolve_binary_operator(BinaryOperator::kAdd, kTimestampTypeId, kTimestampTypeId));
    EXPECT_FALSE(resolve_binary_operator(BinaryOperator::kAnd, kInt64TypeId, kInt64TypeId));
    EXPECT_FALSE(resolve_binary_operator(BinaryOperator::kEqual, kUnknownTypeId, kUnknownTypeId));
}

}  // namespace
}  // namespace db::types
