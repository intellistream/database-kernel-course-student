#include <cstdint>
#include <type_traits>

#include "db/types/operator.hpp"

static_assert(std::is_same_v<std::underlying_type_t<db::types::UnaryOperator>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<db::types::BinaryOperator>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<db::types::BooleanOperator>, std::uint8_t>);
static_assert(db::types::is_logical_operator(db::types::BinaryOperator::kAnd));
static_assert(db::types::is_arithmetic_operator(db::types::BinaryOperator::kAdd));
static_assert(db::types::is_comparison_operator(db::types::BinaryOperator::kEqual));
static_assert(!db::types::is_scalar_binary_operator(db::types::BinaryOperator::kOr));
