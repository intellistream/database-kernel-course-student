#include <algorithm>
#include <compare>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/checked.hpp"
#include "db/base/lab_missing.hpp"
#include "db/planner/preprocess.hpp"
#include "db/types/type_registry.hpp"

#include "preprocess_internal.hpp"

namespace db::planner {
namespace {

template <typename... Visitors>
struct Overloaded final : Visitors... {
    using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

using ExpressionNode = PreprocessedExpression::Node;
using EmbeddedQueryPreprocessor = detail::EmbeddedQueryPreprocessor;

[[maybe_unused]] auto
make_expression(const sql::SourceSpan span, const types::DataType data_type, ExpressionNode node)
    -> PreprocessedExpressionPtr {
    return std::make_unique<PreprocessedExpression>(span, data_type, std::move(node));
}

[[maybe_unused]] auto make_literal(const sql::SourceSpan span, types::Value value)
    -> PreprocessedExpressionPtr {
    const types::DataType data_type = value.data_type();
    return make_expression(span, data_type, QueryLiteralExpression{.value = std::move(value)});
}

[[maybe_unused]] auto make_truth_literal(const sql::SourceSpan span, const types::SqlTruth truth)
    -> PreprocessedExpressionPtr {
    switch (truth) {
        case types::SqlTruth::kFalse:
            return make_literal(span, types::Value::boolean(false));
        case types::SqlTruth::kTrue:
            return make_literal(span, types::Value::boolean(true));
        case types::SqlTruth::kUnknown:
            return make_literal(span, types::Value::null(types::kBoolType));
    }
    return make_literal(span, types::Value::null(types::kBoolType));
}

[[maybe_unused]] auto preprocess_subquery(
    [[maybe_unused]] PreprocessedQueryPtr& query,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query,
    [[maybe_unused]] const char* const absent_error
) -> base::Status {
    // LAB-S04.Q01:R09-BEGIN
    base::lab::missing_algorithm("S04.Q01:R09");
    // LAB-S04.Q01:R09-END
}

[[maybe_unused]] auto value_truth([[maybe_unused]] const types::Value& value)
    -> base::Result<types::SqlTruth> {
    // LAB-S04.Q01:R10-BEGIN
    base::lab::missing_algorithm("S04.Q01:R10");
    // LAB-S04.Q01:R10-END
}

[[maybe_unused]] auto literal_value([[maybe_unused]] const PreprocessedExpression& expression)
    -> base::Result<std::optional<types::Value>> {
    // LAB-S04.Q01:R11-BEGIN
    base::lab::missing_algorithm("S04.Q01:R11");
    // LAB-S04.Q01:R11-END
}

[[maybe_unused]] auto compare_values(
    [[maybe_unused]] const types::Value& left,
    [[maybe_unused]] const types::Value& right,
    [[maybe_unused]] const types::BinaryOperator operation
) -> base::Result<types::SqlTruth> {
    // LAB-S04.Q01:R12-BEGIN
    base::lab::missing_algorithm("S04.Q01:R12");
    // LAB-S04.Q01:R12-END
}

[[maybe_unused]] auto fold_integer_binary(
    [[maybe_unused]] const std::int64_t left,
    [[maybe_unused]] const std::int64_t right,
    [[maybe_unused]] const types::BinaryOperator operation
) -> base::Result<types::Value> {
    // LAB-S04.Q01:R13-BEGIN
    base::lab::missing_algorithm("S04.Q01:R13");
    // LAB-S04.Q01:R13-END
}

[[maybe_unused]] auto fold_float_binary(
    [[maybe_unused]] const float left,
    [[maybe_unused]] const float right,
    [[maybe_unused]] const types::BinaryOperator operation
) -> base::Result<types::Value> {
    // LAB-S04.Q01:R14-BEGIN
    base::lab::missing_algorithm("S04.Q01:R14");
    // LAB-S04.Q01:R14-END
}

[[maybe_unused]] auto fold_binary_literals(
    [[maybe_unused]] const types::Value& left,
    [[maybe_unused]] const types::Value& right,
    [[maybe_unused]] const types::BinaryOperator operation,
    [[maybe_unused]] const types::DataType result_type
) -> base::Result<std::optional<types::Value>> {
    // LAB-S04.Q01:R15-BEGIN
    base::lab::missing_algorithm("S04.Q01:R15");
    // LAB-S04.Q01:R15-END
}

[[maybe_unused]] auto
is_implicit_relabel_cast([[maybe_unused]] const QueryCastExpression& cast) noexcept -> bool {
    // LAB-S04.Q01:R16-BEGIN
    base::lab::missing_algorithm("S04.Q01:R16");
    // LAB-S04.Q01:R16-END
}

[[maybe_unused]] auto fold_binary(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] const types::DataType data_type,
    [[maybe_unused]] QueryBinaryExpression binary,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R17-BEGIN
    base::lab::missing_algorithm("S04.Q01:R17");
    // LAB-S04.Q01:R17-END
}

[[maybe_unused]] auto fold_unary(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] const types::DataType data_type,
    [[maybe_unused]] QueryUnaryExpression unary,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R18-BEGIN
    base::lab::missing_algorithm("S04.Q01:R18");
    // LAB-S04.Q01:R18-END
}

[[maybe_unused]] void flatten_boolean_operands(
    [[maybe_unused]] const types::BooleanOperator operation,
    [[maybe_unused]] std::vector<PreprocessedExpressionPtr>& source,
    [[maybe_unused]] std::vector<PreprocessedExpressionPtr>& destination
) {
    // LAB-S04.Q01:R19-BEGIN
    base::lab::missing_algorithm("S04.Q01:R19");
    // LAB-S04.Q01:R19-END
}

[[maybe_unused]] auto
boolean_identity([[maybe_unused]] const types::BooleanOperator operation) noexcept
    -> types::SqlTruth {
    // LAB-S04.Q01:R20-BEGIN
    base::lab::missing_algorithm("S04.Q01:R20");
    // LAB-S04.Q01:R20-END
}

[[maybe_unused]] auto combine_boolean_truth(
    [[maybe_unused]] const types::BooleanOperator operation,
    [[maybe_unused]] const types::SqlTruth left,
    [[maybe_unused]] const types::SqlTruth right
) noexcept -> types::SqlTruth {
    // LAB-S04.Q01:R21-BEGIN
    base::lab::missing_algorithm("S04.Q01:R21");
    // LAB-S04.Q01:R21-END
}

[[maybe_unused]] auto
boolean_literal_truth([[maybe_unused]] const PreprocessedExpressionPtr& operand)
    -> base::Result<std::optional<types::SqlTruth>> {
    // LAB-S04.Q01:R22-BEGIN
    base::lab::missing_algorithm("S04.Q01:R22");
    // LAB-S04.Q01:R22-END
}

[[maybe_unused]] auto make_not_boolean(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] std::vector<PreprocessedExpressionPtr> operands
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R23-BEGIN
    base::lab::missing_algorithm("S04.Q01:R23");
    // LAB-S04.Q01:R23-END
}

[[maybe_unused]] auto finish_boolean_expression(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] const types::BooleanOperator operation,
    [[maybe_unused]] std::vector<PreprocessedExpressionPtr> operands
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R24-BEGIN
    base::lab::missing_algorithm("S04.Q01:R24");
    // LAB-S04.Q01:R24-END
}

[[maybe_unused]] auto make_boolean(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] const types::BooleanOperator operation,
    [[maybe_unused]] std::vector<PreprocessedExpressionPtr> operands
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R25-BEGIN
    base::lab::missing_algorithm("S04.Q01:R25");
    // LAB-S04.Q01:R25-END
}

[[maybe_unused]] auto fold_boolean(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] QueryBooleanExpression boolean,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R26-BEGIN
    base::lab::missing_algorithm("S04.Q01:R26");
    // LAB-S04.Q01:R26-END
}

[[maybe_unused]] auto fold_is_null(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] QueryIsNullExpression null_test,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R27-BEGIN
    base::lab::missing_algorithm("S04.Q01:R27");
    // LAB-S04.Q01:R27-END
}

[[maybe_unused]] auto fold_in_list(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] QueryInListExpression in_list,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R28-BEGIN
    base::lab::missing_algorithm("S04.Q01:R28");
    // LAB-S04.Q01:R28-END
}

[[maybe_unused]] auto fold_between(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] QueryBetweenExpression between,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R29-BEGIN
    base::lab::missing_algorithm("S04.Q01:R29");
    // LAB-S04.Q01:R29-END
}

[[maybe_unused]] auto fold_cast(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] const types::DataType data_type,
    [[maybe_unused]] QueryCastExpression cast,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R30-BEGIN
    base::lab::missing_algorithm("S04.Q01:R30");
    // LAB-S04.Q01:R30-END
}

[[maybe_unused]] auto fold_aggregate(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] const types::DataType data_type,
    [[maybe_unused]] QueryAggregateExpression aggregate,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R31-BEGIN
    base::lab::missing_algorithm("S04.Q01:R31");
    // LAB-S04.Q01:R31-END
}

[[maybe_unused]] auto fold_in_subquery(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] const types::DataType data_type,
    [[maybe_unused]] QueryInSubqueryExpression in_subquery,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R32-BEGIN
    base::lab::missing_algorithm("S04.Q01:R32");
    // LAB-S04.Q01:R32-END
}

[[maybe_unused]] auto take_conjuncts([[maybe_unused]] PreprocessedExpressionPtr expression)
    -> std::vector<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R33-BEGIN
    base::lab::missing_algorithm("S04.Q01:R33");
    // LAB-S04.Q01:R33-END
}

[[maybe_unused]] auto branch_contains(
    [[maybe_unused]] const std::vector<PreprocessedExpressionPtr>& branch,
    [[maybe_unused]] const PreprocessedExpression& candidate
) -> bool {
    // LAB-S04.Q01:R34-BEGIN
    base::lab::missing_algorithm("S04.Q01:R34");
    // LAB-S04.Q01:R34-END
}

[[maybe_unused]] void erase_matching(
    [[maybe_unused]] std::vector<PreprocessedExpressionPtr>& branch,
    [[maybe_unused]] const PreprocessedExpression& candidate
) {
    // LAB-S04.Q01:R35-BEGIN
    base::lab::missing_algorithm("S04.Q01:R35");
    // LAB-S04.Q01:R35-END
}

auto canonicalize_qualification(PreprocessedExpressionPtr expression)
    -> base::Result<PreprocessedExpressionPtr>;

// Preprocessing does not assign a volatility/error contract to arbitrary
// expressions. Restrict inverse distribution to total leaves and comparisons
// over them: this preserves the number and order of potentially erroring
// expressions such as division until execution evaluates them.
[[maybe_unused]] auto
is_total_leaf([[maybe_unused]] const PreprocessedExpression& expression) noexcept -> bool {
    // LAB-S04.Q01:R36-BEGIN
    base::lab::missing_algorithm("S04.Q01:R36");
    // LAB-S04.Q01:R36-END
}

[[maybe_unused]] auto
is_safe_factor([[maybe_unused]] const PreprocessedExpression& expression) noexcept -> bool {
    // LAB-S04.Q01:R37-BEGIN
    base::lab::missing_algorithm("S04.Q01:R37");
    // LAB-S04.Q01:R37-END
}

[[maybe_unused]] auto factor_or(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] std::vector<PreprocessedExpressionPtr> disjuncts
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R38-BEGIN
    base::lab::missing_algorithm("S04.Q01:R38");
    // LAB-S04.Q01:R38-END
}

[[maybe_unused]] auto canonicalize_boolean(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] QueryBooleanExpression boolean
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R39-BEGIN
    base::lab::missing_algorithm("S04.Q01:R39");
    // LAB-S04.Q01:R39-END
}

[[maybe_unused]] auto
canonicalize_qualification([[maybe_unused]] PreprocessedExpressionPtr expression)
    -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R40-BEGIN
    base::lab::missing_algorithm("S04.Q01:R40");
    // LAB-S04.Q01:R40-END
}
}  // namespace

namespace detail {

[[maybe_unused]] auto fold_expression_ptr(
    [[maybe_unused]] PreprocessedExpressionPtr expression,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R41-BEGIN
    base::lab::missing_algorithm("S04.Q01:R41");
    // LAB-S04.Q01:R41-END
}

[[maybe_unused]] auto preprocess_qualification_ptr(
    [[maybe_unused]] PreprocessedExpressionPtr expression,
    [[maybe_unused]] const EmbeddedQueryPreprocessor preprocess_query
) -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S04.Q01:R42-BEGIN
    base::lab::missing_algorithm("S04.Q01:R42");
    // LAB-S04.Q01:R42-END
}
}  // namespace detail

[[maybe_unused]] auto fold_constant_expression([[maybe_unused]] PreprocessedExpression expression)
    -> base::Result<PreprocessedExpression> {
    // LAB-S04.Q01:R43-BEGIN
    base::lab::missing_algorithm("S04.Q01:R43");
    // LAB-S04.Q01:R43-END
}

[[maybe_unused]] auto preprocess_qualification([[maybe_unused]] PreprocessedExpression expression)
    -> base::Result<PreprocessedExpression> {
    // LAB-S04.Q01:R44-BEGIN
    base::lab::missing_algorithm("S04.Q01:R44");
    // LAB-S04.Q01:R44-END
}
}  // namespace db::planner
