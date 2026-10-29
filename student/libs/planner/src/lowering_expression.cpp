#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/type_registry.hpp"

#include "lowering_internal.hpp"

namespace db::planner::detail {
namespace {

struct SubplanExpressionState final {
    LoweringContext& context;
    std::vector<PlanSubPlan>& subplans;
};

struct ExpressionLoweringState final {
    const InputLayout& input;
    ParameterNamespace& parameters;
    SubplanExpressionState* subplan{nullptr};
    AggregateExpressionContext* aggregate{nullptr};
};

[[maybe_unused]] auto make_expression(
    const sql::SourceSpan span,
    const types::DataType data_type,
    PlanExpression::Node node
) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(lower_source_span(span), data_type, std::move(node));
}

[[maybe_unused]] auto lower_cast_form([[maybe_unused]] const QueryCastForm form) noexcept
    -> PlanCastForm {
    // LAB-S02.Q05:R14-BEGIN
    base::lab::missing_algorithm("S02.Q05:R14");
    // LAB-S02.Q05:R14-END
}

[[maybe_unused]] auto make_aggregate_input_reference(
    [[maybe_unused]] const sql::SourceSpan span,
    [[maybe_unused]] const types::DataType data_type,
    [[maybe_unused]] const std::size_t index
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R15-BEGIN
    base::lab::missing_algorithm("S02.Q05:R15");
    // LAB-S02.Q05:R15-END
}

[[maybe_unused]] auto find_group_expression(
    [[maybe_unused]] const std::vector<PreprocessedExpressionPtr>& groups,
    [[maybe_unused]] const PreprocessedExpression& candidate
) -> std::optional<std::size_t> {
    // LAB-S02.Q05:R16-BEGIN
    base::lab::missing_algorithm("S02.Q05:R16");
    // LAB-S02.Q05:R16-END
}

[[maybe_unused]] auto same_aggregate(
    [[maybe_unused]] const QueryAggregateExpression& left,
    [[maybe_unused]] const QueryAggregateExpression& right
) -> bool {
    // LAB-S02.Q05:R17-BEGIN
    base::lab::missing_algorithm("S02.Q05:R17");
    // LAB-S02.Q05:R17-END
}

[[maybe_unused]] auto register_aggregate(
    [[maybe_unused]] AggregateExpressionContext& context,
    [[maybe_unused]] const QueryAggregateExpression& aggregate,
    [[maybe_unused]] const types::DataType data_type
) -> std::size_t {
    // LAB-S02.Q05:R18-BEGIN
    base::lab::missing_algorithm("S02.Q05:R18");
    // LAB-S02.Q05:R18-END
}

auto lower_validated_expression_impl(
    const PreprocessedExpression& expression,
    ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr>;

[[maybe_unused]] auto lower_expression_pointer(
    [[maybe_unused]] const PreprocessedExpressionPtr& expression,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R19-BEGIN
    base::lab::missing_algorithm("S02.Q05:R19");
    // LAB-S02.Q05:R19-END
}

[[maybe_unused]] auto lower_expression_list(
    [[maybe_unused]] const std::vector<PreprocessedExpressionPtr>& expressions,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<std::vector<PlanExpressionPtr>> {
    // LAB-S02.Q05:R20-BEGIN
    base::lab::missing_algorithm("S02.Q05:R20");
    // LAB-S02.Q05:R20-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryLiteralExpression& node,
    [[maybe_unused]] const ExpressionLoweringState& /*state*/
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R21-BEGIN
    base::lab::missing_algorithm("S02.Q05:R21");
    // LAB-S02.Q05:R21-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryColumnReference& node,
    [[maybe_unused]] const ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R22-BEGIN
    base::lab::missing_algorithm("S02.Q05:R22");
    // LAB-S02.Q05:R22-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryParameterReference& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R23-BEGIN
    base::lab::missing_algorithm("S02.Q05:R23");
    // LAB-S02.Q05:R23-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryExecutorParameterReference& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R24-BEGIN
    base::lab::missing_algorithm("S02.Q05:R24");
    // LAB-S02.Q05:R24-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryUnaryExpression& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R25-BEGIN
    base::lab::missing_algorithm("S02.Q05:R25");
    // LAB-S02.Q05:R25-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryBinaryExpression& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R26-BEGIN
    base::lab::missing_algorithm("S02.Q05:R26");
    // LAB-S02.Q05:R26-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryBooleanExpression& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R27-BEGIN
    base::lab::missing_algorithm("S02.Q05:R27");
    // LAB-S02.Q05:R27-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryIsNullExpression& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R28-BEGIN
    base::lab::missing_algorithm("S02.Q05:R28");
    // LAB-S02.Q05:R28-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryInListExpression& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R29-BEGIN
    base::lab::missing_algorithm("S02.Q05:R29");
    // LAB-S02.Q05:R29-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryBetweenExpression& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R30-BEGIN
    base::lab::missing_algorithm("S02.Q05:R30");
    // LAB-S02.Q05:R30-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryCastExpression& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R31-BEGIN
    base::lab::missing_algorithm("S02.Q05:R31");
    // LAB-S02.Q05:R31-END
}

[[maybe_unused]] auto make_subplan_reference(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] PreprocessedQuery& query,
    [[maybe_unused]] const PlanSubPlanStrategy strategy,
    [[maybe_unused]] PlanExpressionPtr input_expression,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S07.Q08:R07-BEGIN
    base::lab::missing_algorithm("S07.Q08:R07");
    // LAB-S07.Q08:R07-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryAggregateExpression& aggregate,
    [[maybe_unused]] const ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R32-BEGIN
    base::lab::missing_algorithm("S02.Q05:R32");
    // LAB-S02.Q05:R32-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryScalarSubqueryExpression& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R33-BEGIN
    base::lab::missing_algorithm("S02.Q05:R33");
    // LAB-S02.Q05:R33-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryExistsExpression& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R34-BEGIN
    base::lab::missing_algorithm("S02.Q05:R34");
    // LAB-S02.Q05:R34-END
}

[[maybe_unused]] auto lower_query_node(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryInSubqueryExpression& node,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R35-BEGIN
    base::lab::missing_algorithm("S02.Q05:R35");
    // LAB-S02.Q05:R35-END
}

[[maybe_unused]] auto lower_validated_expression_impl(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] ExpressionLoweringState& state
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R36-BEGIN
    base::lab::missing_algorithm("S02.Q05:R36");
    // LAB-S02.Q05:R36-END
}
}  // namespace

auto lowering_error(std::string message) -> base::Error {
    return {base::ErrorCode::kPlan, std::move(message)};
}

auto lowering_unsupported(std::string message) -> base::Error {
    return {base::ErrorCode::kUnsupported, std::move(message)};
}

[[maybe_unused]] auto
join_schema([[maybe_unused]] const PlanSchema& outer, [[maybe_unused]] const PlanSchema& inner)
    -> PlanSchema {
    // LAB-S02.Q05:R37-BEGIN
    base::lab::missing_algorithm("S02.Q05:R37");
    // LAB-S02.Q05:R37-END
}

[[maybe_unused]] auto single_relation_layout(
    [[maybe_unused]] const QueryRangeTableIndex range_table_index,
    [[maybe_unused]] PlanSchema schema
) -> InputLayout {
    // LAB-S02.Q05:R38-BEGIN
    base::lab::missing_algorithm("S02.Q05:R38");
    // LAB-S02.Q05:R38-END
}

[[maybe_unused]] auto
join_layout([[maybe_unused]] const InputLayout& outer, [[maybe_unused]] const InputLayout& inner)
    -> InputLayout {
    // LAB-S02.Q05:R39-BEGIN
    base::lab::missing_algorithm("S02.Q05:R39");
    // LAB-S02.Q05:R39-END
}

[[maybe_unused]] auto resolve_input_column(
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] const QueryColumnReference& column
) -> base::Result<PlanColumnIndex> {
    // LAB-S02.Q05:R40-BEGIN
    base::lab::missing_algorithm("S02.Q05:R40");
    // LAB-S02.Q05:R40-END
}

[[maybe_unused]] auto lower_expression(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] ParameterNamespace& parameters
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R41-BEGIN
    base::lab::missing_algorithm("S02.Q05:R41");
    // LAB-S02.Q05:R41-END
}

[[maybe_unused]] auto lower_prevalidated_expression(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] ParameterNamespace& parameters
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R42-BEGIN
    base::lab::missing_algorithm("S02.Q05:R42");
    // LAB-S02.Q05:R42-END
}

[[maybe_unused]] auto rewrite_aggregate_expression(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] AggregateExpressionContext& context
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R43-BEGIN
    base::lab::missing_algorithm("S02.Q05:R43");
    // LAB-S02.Q05:R43-END
}

[[maybe_unused]] auto lower_expression_with_subplans(
    [[maybe_unused]] PreprocessedExpression& expression,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] LoweringContext& context,
    [[maybe_unused]] std::vector<PlanSubPlan>& subplans
) -> base::Result<PlanExpressionPtr> {
    // LAB-S07.Q08:R08-BEGIN
    base::lab::missing_algorithm("S07.Q08:R08");
    // LAB-S07.Q08:R08-END
}

[[maybe_unused]] auto lower_boolean_conjunction(
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> expressions,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] ParameterNamespace& parameters,
    [[maybe_unused]] const std::string& subject
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R44-BEGIN
    base::lab::missing_algorithm("S02.Q05:R44");
    // LAB-S02.Q05:R44-END
}

[[maybe_unused]] auto lower_index_bound_value(
    [[maybe_unused]] const IndexPathBoundValue& value,
    [[maybe_unused]] ParameterNamespace& parameters
) -> base::Result<PlanExpressionPtr> {
    // LAB-S02.Q05:R45-BEGIN
    base::lab::missing_algorithm("S02.Q05:R45");
    // LAB-S02.Q05:R45-END
}

[[maybe_unused]] auto verify_catalog_generation([[maybe_unused]] const LoweringContext& context)
    -> base::Status {
    // LAB-S02.Q05:R46-BEGIN
    base::lab::missing_algorithm("S02.Q05:R46");
    // LAB-S02.Q05:R46-END
}
}  // namespace db::planner::detail
