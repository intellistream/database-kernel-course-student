#include "db/types/operator_registry.hpp"

#include <algorithm>
#include <array>
#include <span>

#include "db/base/lab_missing.hpp"

namespace db::types {
namespace {

struct OperatorFamily final {
    std::span<const BinaryOperator> operators;
    TypeId operand;
    TypeId result;
};

constexpr std::array kLogicalOperators{BinaryOperator::kAnd, BinaryOperator::kOr};
constexpr std::array kArithmeticOperators{
    BinaryOperator::kAdd,
    BinaryOperator::kSubtract,
    BinaryOperator::kMultiply,
    BinaryOperator::kDivide,
};
constexpr std::array kIntegerArithmeticOperators{
    BinaryOperator::kAdd,
    BinaryOperator::kSubtract,
    BinaryOperator::kMultiply,
    BinaryOperator::kDivide,
    BinaryOperator::kModulo,
};
constexpr std::array kComparisonOperators{
    BinaryOperator::kEqual,
    BinaryOperator::kNotEqual,
    BinaryOperator::kLess,
    BinaryOperator::kLessEqual,
    BinaryOperator::kGreater,
    BinaryOperator::kGreaterEqual,
};

constexpr std::array kOperatorFamilies{
    OperatorFamily{kLogicalOperators, kBoolTypeId, kBoolTypeId},
    OperatorFamily{kIntegerArithmeticOperators, kInt64TypeId, kInt64TypeId},
    OperatorFamily{kArithmeticOperators, kFloat32TypeId, kFloat32TypeId},
    OperatorFamily{kComparisonOperators, kBoolTypeId, kBoolTypeId},
    OperatorFamily{kComparisonOperators, kInt64TypeId, kBoolTypeId},
    OperatorFamily{kComparisonOperators, kFloat32TypeId, kBoolTypeId},
    OperatorFamily{kComparisonOperators, kCharTypeId, kBoolTypeId},
    OperatorFamily{kComparisonOperators, kTimestampTypeId, kBoolTypeId},
};

}  // namespace

[[maybe_unused]] auto resolve_binary_operator(
    [[maybe_unused]] const BinaryOperator operation,
    [[maybe_unused]] const TypeId left,
    [[maybe_unused]] const TypeId right
) noexcept -> std::optional<TypeId> {
    static_cast<void>(operation);
    static_cast<void>(left);
    static_cast<void>(right);
    // LAB-S02.Q02:R06-BEGIN
    base::lab::missing_algorithm("S02.Q02:R06");
    // LAB-S02.Q02:R06-END
}

}  // namespace db::types
