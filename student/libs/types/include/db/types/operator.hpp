#pragma once

#include <cstdint>

namespace db::types {

// Resolved scalar operators shared by bound SQL, physical plans, and
// expression execution. Source SQL keeps syntax-owned enums and the binder
// converts them once after type resolution.
enum class UnaryOperator : std::uint8_t {
    kPlus,
    kMinus,
    kNot,
};

enum class BinaryOperator : std::uint8_t {
    kAnd,
    kOr,
    kAdd,
    kSubtract,
    kMultiply,
    kDivide,
    kModulo,
    kEqual,
    kNotEqual,
    kLess,
    kLessEqual,
    kGreater,
    kGreaterEqual,
};

// Boolean expressions are variadic in the SQL and plan trees, so their
// operation is distinct from the binary operator registry's typed AND/OR
// signatures.
enum class BooleanOperator : std::uint8_t {
    kAnd,
    kOr,
    kNot,
};

constexpr auto is_logical_operator(const BinaryOperator operation) noexcept -> bool {
    return operation == BinaryOperator::kAnd || operation == BinaryOperator::kOr;
}

constexpr auto is_arithmetic_operator(const BinaryOperator operation) noexcept -> bool {
    switch (operation) {
        case BinaryOperator::kAdd:
        case BinaryOperator::kSubtract:
        case BinaryOperator::kMultiply:
        case BinaryOperator::kDivide:
        case BinaryOperator::kModulo:
            return true;
        case BinaryOperator::kAnd:
        case BinaryOperator::kOr:
        case BinaryOperator::kEqual:
        case BinaryOperator::kNotEqual:
        case BinaryOperator::kLess:
        case BinaryOperator::kLessEqual:
        case BinaryOperator::kGreater:
        case BinaryOperator::kGreaterEqual:
            return false;
    }
    return false;
}

constexpr auto is_comparison_operator(const BinaryOperator operation) noexcept -> bool {
    switch (operation) {
        case BinaryOperator::kEqual:
        case BinaryOperator::kNotEqual:
        case BinaryOperator::kLess:
        case BinaryOperator::kLessEqual:
        case BinaryOperator::kGreater:
        case BinaryOperator::kGreaterEqual:
            return true;
        case BinaryOperator::kAnd:
        case BinaryOperator::kOr:
        case BinaryOperator::kAdd:
        case BinaryOperator::kSubtract:
        case BinaryOperator::kMultiply:
        case BinaryOperator::kDivide:
        case BinaryOperator::kModulo:
            return false;
    }
    return false;
}

constexpr auto is_scalar_binary_operator(const BinaryOperator operation) noexcept -> bool {
    return is_arithmetic_operator(operation) || is_comparison_operator(operation);
}

}  // namespace db::types
