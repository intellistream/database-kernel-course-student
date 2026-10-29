#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/base/result.hpp"
#include "db/planner/lowering.hpp"
#include "db/planner/path.hpp"

namespace db::planner::detail {

[[maybe_unused]] constexpr auto
lower_source_span([[maybe_unused]] const sql::SourceSpan span) noexcept -> PlanSourceSpan {
    // LAB-S02.Q05:R47-BEGIN
    base::lab::missing_algorithm("S02.Q05:R47");
    // LAB-S02.Q05:R47-END
}

struct InputRelationLayout final {
    QueryRangeTableIndex range_table_index{};
    std::size_t offset{0U};
    PlanSchema schema;
};

// This is short-lived lowering state, not plan metadata.  RTE identity cannot
// be recovered from a physical PlanSchema when a table is self-joined, so it
// remains here until every logical column reference has been rewritten to an
// input position.
struct InputLayout final {
    PlanSchema schema;
    std::vector<InputRelationLayout> relations;
};

struct IndexOrderProperty final {
    QueryRangeTableIndex range_table_index{};
    IndexScanPath path;
};

struct LoweredPlanInput final {
    Plan plan;
    InputLayout layout;
    std::optional<IndexOrderProperty> ordering{};
};

struct ProjectionTiming final {
    bool project_before_order{false};
    bool distinct{false};
};

struct LoweringContext final {
    const MetadataPort& metadata_port;
    ParameterNamespace& parameters;
    std::uint64_t catalog_generation{0U};
};

struct AggregateExpressionRegistration final {
    std::reference_wrapper<const QueryAggregateExpression> aggregate;
    types::DataType data_type;
};

struct AggregateExpressionContext final {
    ParameterNamespace& parameters;
    const std::vector<PreprocessedExpressionPtr>& group_by;
    std::vector<AggregateExpressionRegistration> aggregates;
};

// Correlation rewriting is deliberately separate from physical SubPlan
// construction.  It owns only the mapping constructed for one child query;
// the completed physical plan owns the moved expressions and parameter
// references. An already rewritten nested child may consume ancestor slots,
// hence the explicit inherited marker used by execution-state setup.
struct SubPlanCorrelation final {
    std::vector<PlanExpressionPtr> arguments;
    std::vector<PlanParameterReference> parameters;
    bool uses_inherited_parameters{false};
};

auto lowering_error(std::string message) -> base::Error;
auto lowering_unsupported(std::string message) -> base::Error;
auto verify_catalog_generation(const LoweringContext& context) -> base::Status;
auto lower_sort_direction(sql::SortDirection direction) -> base::Result<PlanSortDirection>;
auto lower_null_order(sql::NullOrder order) -> base::Result<PlanNullOrder>;

auto lower_expression(
    const PreprocessedExpression& expression,
    const InputLayout& input,
    ParameterNamespace& parameters
) -> base::Result<PlanExpressionPtr>;

// Recursive scalar lowering after the owning query or expression boundary has
// validated the complete expression tree once.
auto lower_prevalidated_expression(
    const PreprocessedExpression& expression,
    const InputLayout& input,
    ParameterNamespace& parameters
) -> base::Result<PlanExpressionPtr>;

// Rewrites one validated post-aggregate expression. The shared scalar
// traversal preserves structural child order while this state owns GROUP BY
// matching and aggregate registration across projection, HAVING, and ORDER BY.
auto rewrite_aggregate_expression(
    const PreprocessedExpression& expression,
    AggregateExpressionContext& context
) -> base::Result<PlanExpressionPtr>;

// Converts scalar/EXISTS/IN expression subqueries into references owned by
// one surrounding SubPlanEvalPlan. The supplied logical expression is mutable
// only because correlation rewrite replaces parent-level column references in
// the owned child query with planner executor-parameter references; no bound
// AST or binder scope is retained.
auto lower_expression_with_subplans(
    PreprocessedExpression& expression,
    const InputLayout& input,
    LoweringContext& context,
    std::vector<PlanSubPlan>& subplans
) -> base::Result<PlanExpressionPtr>;

// Materializes one already-validated subquery terminal. Expression recursion
// remains in the shared scalar lowerer; this boundary only owns correlation,
// child-plan construction, and the surrounding SubPlan collection.
auto materialize_expression_subplan(
    PreprocessedQuery& query,
    PlanSubPlanStrategy strategy,
    types::DataType result_type,
    PlanExpressionPtr input_expression,
    const InputLayout& input,
    LoweringContext& context,
    std::vector<PlanSubPlan>& subplans
) -> base::Result<PlanSubPlanIndex>;

// Rewrites exactly the references to this expression subquery's immediate
// parent input into statement-wide executor slots.  It walks nested expression
// subqueries far enough to rewrite references to older ancestors, while each
// nested subplan later owns allocation of its own immediate-parent slots.
auto rewrite_subplan_correlation(
    PreprocessedQuery& query,
    const InputLayout& parent_input,
    LoweringContext& context
) -> base::Result<SubPlanCorrelation>;

auto default_expression_name(
    const PreprocessedExpression& expression,
    const InputLayout& input,
    std::size_t ordinal
) -> std::string;

auto lower_index_bound_value(const IndexPathBoundValue& value, ParameterNamespace& parameters)
    -> base::Result<PlanExpressionPtr>;

// This adapter owns the only boolean-tree construction used by table and join
// lowering. Source expressions are borrowed for the duration of lowering and
// never stored in a physical plan.
auto lower_boolean_conjunction(
    std::span<const PreprocessedExpression* const> expressions,
    const InputLayout& input,
    ParameterNamespace& parameters,
    const std::string& subject
) -> base::Result<PlanExpressionPtr>;

// Lowers a path snapshot that has already been selected by a path or join
// phase. Keeping this separate from metadata path construction lets strategy
// selection reuse the same scan conversion without another catalog read.
auto lower_base_relation_path(
    const PreprocessedBaseTable& base_table,
    const BaseRelationPathList& paths,
    const BaseRelationPath& selected_path,
    std::span<const PreprocessedExpression* const> filters,
    LoweringContext& context
) -> base::Result<LoweredPlanInput>;

// The strategy adapter returns an empty optional when a FROM tree is outside
// the 2--6 base-relation INNER/CROSS domain. A domain-shaped but malformed
// tree remains an error rather than silently falling back to another plan.
auto lower_small_inner_join_if_applicable(
    const PreprocessedTableReference& from,
    const PreprocessedExpression* where_filter,
    LoweringContext& context
) -> base::Result<std::optional<LoweredPlanInput>>;

auto lower_query_input(
    PreprocessedQuery& query,
    const PreprocessedExpression* filter,
    LoweringContext& context
) -> base::Result<LoweredPlanInput>;

// Converts a boolean expression (including scalar/IN/EXISTS subqueries) into
// one evaluation boundary above an already built input.  The caller supplies a
// mutable planner-owned expression because correlation rewriting consumes no
// binder state but rewrites child outer references into executor slots.
auto append_subplan_filter(
    Plan input,
    const InputLayout& input_layout,
    PreprocessedExpression& filter,
    LoweringContext& context,
    const std::string& subject
) -> base::Result<Plan>;

auto lower_query_plan(PreprocessedQuery& query, LoweringContext& context) -> base::Result<Plan>;

auto lower_insert_plan(PreprocessedInsertStatement& statement, LoweringContext& context)
    -> base::Result<Plan>;
auto lower_update_plan(PreprocessedUpdateStatement& statement, LoweringContext& context)
    -> base::Result<Plan>;
auto lower_delete_plan(PreprocessedDeleteStatement& statement, LoweringContext& context)
    -> base::Result<Plan>;

void set_required_columns(Plan& root);

auto determine_projection_timing(const PreprocessedSelectQuery& select)
    -> base::Result<ProjectionTiming>;
auto make_projection_schema(const std::vector<PlanProjectionItem>& projection)
    -> base::Result<PlanSchema>;
auto lower_sort_items_as_projection_references(
    const std::vector<PreprocessedSortItem>& order_by,
    const std::vector<PreprocessedProjectionItem>& projection,
    const PlanSchema& schema
) -> base::Result<std::vector<PlanSortItem>>;
auto lower_row_count_expression(
    const PreprocessedExpressionPtr& expression,
    ParameterNamespace& parameters,
    const std::string& subject
) -> base::Result<PlanExpressionPtr>;
auto append_unique(Plan input, const PlanSchema& schema) -> Plan;
auto append_order_or_top_n(
    Plan input,
    const PlanSchema& schema,
    std::vector<PlanSortItem> order_by,
    const PreprocessedExpressionPtr& limit,
    const PreprocessedExpressionPtr& offset,
    ParameterNamespace& parameters
) -> base::Result<Plan>;
auto append_limit_if_requested(
    Plan input,
    const PlanSchema& schema,
    const PreprocessedExpressionPtr& limit,
    const PreprocessedExpressionPtr& offset,
    ParameterNamespace& parameters
) -> base::Result<Plan>;

auto lower_select_operators(
    PreprocessedQuery& query,
    LoweredPlanInput input,
    LoweringContext& context
) -> base::Result<Plan>;
auto lower_aggregate_select_operators(
    const PreprocessedSelectQuery& select,
    LoweredPlanInput input,
    LoweringContext& context
) -> base::Result<Plan>;

auto join_schema(const PlanSchema& outer, const PlanSchema& inner) -> PlanSchema;
auto resolve_input_column(const InputLayout& input, const QueryColumnReference& column)
    -> base::Result<PlanColumnIndex>;
auto join_layout(const InputLayout& outer, const InputLayout& inner) -> InputLayout;
auto single_relation_layout(QueryRangeTableIndex range_table_index, PlanSchema schema)
    -> InputLayout;

}  // namespace db::planner::detail
