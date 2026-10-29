#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/lab_missing.hpp"

#include "lowering_internal.hpp"

namespace db::planner::detail {

[[maybe_unused]] auto lower_sort_direction([[maybe_unused]] const sql::SortDirection direction)
    -> base::Result<PlanSortDirection> {
    static_cast<void>(direction);
    // LAB-S02.Q05:R110-BEGIN
    base::lab::missing_algorithm("S02.Q05:R110");
    // LAB-S02.Q05:R110-END
}

[[maybe_unused]] auto lower_null_order([[maybe_unused]] const sql::NullOrder order)
    -> base::Result<PlanNullOrder> {
    static_cast<void>(order);
    // LAB-S02.Q05:R111-BEGIN
    base::lab::missing_algorithm("S02.Q05:R111");
    // LAB-S02.Q05:R111-END
}

namespace {

[[maybe_unused]] auto build_result_plan(
    [[maybe_unused]] Plan input,
    [[maybe_unused]] std::vector<PreprocessedProjectionItem>& projection,
    [[maybe_unused]] const InputLayout& input_layout,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<Plan> {
    static_cast<void>(input);
    static_cast<void>(projection);
    static_cast<void>(input_layout);
    static_cast<void>(context);
    // LAB-S04.Q02:R30-BEGIN
    base::lab::missing_algorithm("S04.Q02:R30");
    // LAB-S04.Q02:R30-END
}

[[maybe_unused]] auto lower_sort_items(
    [[maybe_unused]] const std::vector<PreprocessedSortItem>& items,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] ParameterNamespace& parameters
) -> base::Result<std::vector<PlanSortItem>> {
    static_cast<void>(items);
    static_cast<void>(input);
    static_cast<void>(parameters);
    // LAB-S02.Q05:R112-BEGIN
    base::lab::missing_algorithm("S02.Q05:R112");
    // LAB-S02.Q05:R112-END
}

[[maybe_unused]] auto projection_position(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const std::vector<PreprocessedProjectionItem>& projection
) -> std::optional<std::size_t> {
    static_cast<void>(expression);
    static_cast<void>(projection);
    // LAB-S04.Q02:R31-BEGIN
    base::lab::missing_algorithm("S04.Q02:R31");
    // LAB-S04.Q02:R31-END
}

[[maybe_unused]] auto is_row_independent([[maybe_unused]] const PreprocessedExpression& expression)
    -> bool {
    static_cast<void>(expression);
    // LAB-S04.Q04:R18-BEGIN
    base::lab::missing_algorithm("S04.Q04:R18");
    // LAB-S04.Q04:R18-END
}

[[maybe_unused]] auto input_satisfies_order(
    [[maybe_unused]] const std::optional<IndexOrderProperty>& ordering,
    [[maybe_unused]] const std::vector<PreprocessedSortItem>& order_by
) -> bool {
    static_cast<void>(ordering);
    static_cast<void>(order_by);
    // LAB-S04.Q04:R19-BEGIN
    base::lab::missing_algorithm("S04.Q04:R19");
    // LAB-S04.Q04:R19-END
}

[[maybe_unused]] auto select_has_aggregate([[maybe_unused]] const PreprocessedSelectQuery& select)
    -> bool {
    static_cast<void>(select);
    // LAB-S05.Q04:R22-BEGIN
    base::lab::missing_algorithm("S05.Q04:R22");
    // LAB-S05.Q04:R22-END
}

[[maybe_unused]] auto lower_nonaggregate_select_operators(
    [[maybe_unused]] PreprocessedSelectQuery& select,
    [[maybe_unused]] LoweredPlanInput input,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<Plan> {
    static_cast<void>(select);
    static_cast<void>(input);
    static_cast<void>(context);
    // LAB-S02.Q05:R113-BEGIN
    base::lab::missing_algorithm("S02.Q05:R113");
    // LAB-S02.Q05:R113-END
}
}  // namespace

[[maybe_unused]] auto default_expression_name(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] const std::size_t ordinal
) -> std::string {
    static_cast<void>(expression);
    static_cast<void>(input);
    static_cast<void>(ordinal);
    // LAB-S04.Q02:R32-BEGIN
    base::lab::missing_algorithm("S04.Q02:R32");
    // LAB-S04.Q02:R32-END
}

[[maybe_unused]] auto
determine_projection_timing([[maybe_unused]] const PreprocessedSelectQuery& select)
    -> base::Result<ProjectionTiming> {
    static_cast<void>(select);
    // LAB-S04.Q02:R33-BEGIN
    base::lab::missing_algorithm("S04.Q02:R33");
    // LAB-S04.Q02:R33-END
}

[[maybe_unused]] auto
make_projection_schema([[maybe_unused]] const std::vector<PlanProjectionItem>& projection)
    -> base::Result<PlanSchema> {
    static_cast<void>(projection);
    // LAB-S04.Q02:R34-BEGIN
    base::lab::missing_algorithm("S04.Q02:R34");
    // LAB-S04.Q02:R34-END
}

[[maybe_unused]] auto lower_sort_items_as_projection_references(
    [[maybe_unused]] const std::vector<PreprocessedSortItem>& order_by,
    [[maybe_unused]] const std::vector<PreprocessedProjectionItem>& projection,
    [[maybe_unused]] const PlanSchema& schema
) -> base::Result<std::vector<PlanSortItem>> {
    static_cast<void>(order_by);
    static_cast<void>(projection);
    static_cast<void>(schema);
    // LAB-S02.Q05:R114-BEGIN
    base::lab::missing_algorithm("S02.Q05:R114");
    // LAB-S02.Q05:R114-END
}

[[maybe_unused]] auto lower_row_count_expression(
    [[maybe_unused]] const PreprocessedExpressionPtr& expression,
    [[maybe_unused]] ParameterNamespace& parameters,
    [[maybe_unused]] const std::string& subject
) -> base::Result<PlanExpressionPtr> {
    static_cast<void>(expression);
    static_cast<void>(parameters);
    static_cast<void>(subject);
    // LAB-S02.Q05:R115-BEGIN
    base::lab::missing_algorithm("S02.Q05:R115");
    // LAB-S02.Q05:R115-END
}

// LAB-S06.Q03:R06-BEGIN
[[maybe_unused]] auto append_unique([[maybe_unused]] Plan, [[maybe_unused]] const PlanSchema&)
    -> Plan {
    base::lab::missing_algorithm("S06.Q03:R06");
}

// LAB-S06.Q03:R06-END

[[maybe_unused]] auto append_order_or_top_n(
    [[maybe_unused]] Plan input,
    [[maybe_unused]] const PlanSchema& schema,
    [[maybe_unused]] std::vector<PlanSortItem> order_by,
    [[maybe_unused]] const PreprocessedExpressionPtr& limit,
    [[maybe_unused]] const PreprocessedExpressionPtr& offset,
    [[maybe_unused]] ParameterNamespace& parameters
) -> base::Result<Plan> {
    if (limit == nullptr) {
        return Plan{SortPlan{
            .metadata = PlanOperatorMetadata{.output = schema},
            .input = std::make_unique<Plan>(std::move(input)),
            .order_by = std::move(order_by),
        }};
    }
    // LAB-S04.Q05:R08-BEGIN
    static_cast<void>(offset);
    static_cast<void>(parameters);
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S04.Q05:R08: lower ORDER BY LIMIT/OFFSET as Top-N"
        }
    );
    // LAB-S04.Q05:R08-END
}

[[maybe_unused]] auto append_limit_if_requested(
    [[maybe_unused]] Plan input,
    [[maybe_unused]] const PlanSchema& schema,
    [[maybe_unused]] const PreprocessedExpressionPtr& limit,
    [[maybe_unused]] const PreprocessedExpressionPtr& offset,
    [[maybe_unused]] ParameterNamespace& parameters
) -> base::Result<Plan> {
    static_cast<void>(input);
    static_cast<void>(schema);
    static_cast<void>(limit);
    static_cast<void>(offset);
    static_cast<void>(parameters);
    // LAB-S02.Q05:R116-BEGIN
    base::lab::missing_algorithm("S02.Q05:R116");
    // LAB-S02.Q05:R116-END
}

[[maybe_unused]] auto lower_select_operators(
    [[maybe_unused]] PreprocessedQuery& query,
    [[maybe_unused]] LoweredPlanInput input,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<Plan> {
    static_cast<void>(query);
    static_cast<void>(input);
    static_cast<void>(context);
    // LAB-S02.Q05:R117-BEGIN
    base::lab::missing_algorithm("S02.Q05:R117");
    // LAB-S02.Q05:R117-END
}
}  // namespace db::planner::detail
