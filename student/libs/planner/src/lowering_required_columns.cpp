#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/lab_missing.hpp"
#include "db/planner/plan.hpp"

#include "lowering_internal.hpp"

namespace db::planner::detail {
namespace {

using RequiredColumns = std::set<std::size_t>;

template <typename... Visitors>
struct Overloaded final : Visitors... {
    using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

[[maybe_unused]] void add_required_column(
    [[maybe_unused]] RequiredColumns& required,
    [[maybe_unused]] const PlanColumnIndex column
) {
    // LAB-S04.Q02:R01-BEGIN
    base::lab::missing_algorithm("S04.Q02:R01");
    // LAB-S04.Q02:R01-END
}

void collect_expression_columns(const PlanExpression& expression, RequiredColumns& required);

[[maybe_unused]] void collect_expression_pointer_columns(
    [[maybe_unused]] const PlanExpressionPtr& expression,
    [[maybe_unused]] RequiredColumns& required
) {
    // LAB-S04.Q02:R02-BEGIN
    base::lab::missing_algorithm("S04.Q02:R02");
    // LAB-S04.Q02:R02-END
}

[[maybe_unused]] void collect_expression_list_columns(
    [[maybe_unused]] const std::vector<PlanExpressionPtr>& expressions,
    [[maybe_unused]] RequiredColumns& required
) {
    // LAB-S04.Q02:R03-BEGIN
    base::lab::missing_algorithm("S04.Q02:R03");
    // LAB-S04.Q02:R03-END
}

[[maybe_unused]] void collect_expression_columns(
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] RequiredColumns& required
) {
    // LAB-S04.Q02:R04-BEGIN
    base::lab::missing_algorithm("S04.Q02:R04");
    // LAB-S04.Q02:R04-END
}

void propagate_required_columns(Plan& plan, const RequiredColumns& required);

[[maybe_unused]] void propagate_scan_required_columns(
    [[maybe_unused]] std::optional<std::vector<PlanColumnIndex>>& target,
    [[maybe_unused]] const PlanExpressionPtr& filter,
    [[maybe_unused]] const std::vector<PlanIndexBound>* const bounds,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R05-BEGIN
    base::lab::missing_algorithm("S04.Q02:R05");
    // LAB-S04.Q02:R05-END
}

[[maybe_unused]] void collect_order_by_columns(
    [[maybe_unused]] const std::vector<PlanSortItem>& order_by,
    [[maybe_unused]] RequiredColumns& required
) {
    // LAB-S04.Q02:R06-BEGIN
    base::lab::missing_algorithm("S04.Q02:R06");
    // LAB-S04.Q02:R06-END
}

[[maybe_unused]] void collect_limit_offset_columns(
    [[maybe_unused]] const PlanExpressionPtr& limit,
    [[maybe_unused]] const PlanExpressionPtr& offset,
    [[maybe_unused]] RequiredColumns& required
) {
    // LAB-S04.Q02:R07-BEGIN
    base::lab::missing_algorithm("S04.Q02:R07");
    // LAB-S04.Q02:R07-END
}

[[maybe_unused]] auto all_required_columns([[maybe_unused]] const PlanSchema& schema)
    -> RequiredColumns {
    // LAB-S04.Q02:R08-BEGIN
    base::lab::missing_algorithm("S04.Q02:R08");
    // LAB-S04.Q02:R08-END
}

[[maybe_unused]] void distribute_join_required_columns(
    [[maybe_unused]] const RequiredColumns& required,
    [[maybe_unused]] const PlanSchema& outer_schema,
    [[maybe_unused]] RequiredColumns& outer_required,
    [[maybe_unused]] RequiredColumns& inner_required
) {
    // LAB-S04.Q02:R09-BEGIN
    base::lab::missing_algorithm("S04.Q02:R09");
    // LAB-S04.Q02:R09-END
}

[[maybe_unused]] void collect_join_residual_columns(
    [[maybe_unused]] const PlanExpressionPtr& residual_filter,
    [[maybe_unused]] const PlanSchema& outer_schema,
    [[maybe_unused]] RequiredColumns& outer_required,
    [[maybe_unused]] RequiredColumns& inner_required
) {
    // LAB-S04.Q02:R10-BEGIN
    base::lab::missing_algorithm("S04.Q02:R10");
    // LAB-S04.Q02:R10-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] ValuesPlan& /*node*/,
    [[maybe_unused]] const RequiredColumns& /*required*/
) {
    // LAB-S04.Q02:R11-BEGIN
    base::lab::missing_algorithm("S04.Q02:R11");
    // LAB-S04.Q02:R11-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] SeqScanPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R12-BEGIN
    base::lab::missing_algorithm("S04.Q02:R12");
    // LAB-S04.Q02:R12-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] IndexScanPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R13-BEGIN
    base::lab::missing_algorithm("S04.Q02:R13");
    // LAB-S04.Q02:R13-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] ResultPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R14-BEGIN
    base::lab::missing_algorithm("S04.Q02:R14");
    // LAB-S04.Q02:R14-END
}

[[maybe_unused]] [[noreturn]] void propagate_node_required_columns(
    [[maybe_unused]] ModifyTablePlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R15-BEGIN
    base::lab::missing_algorithm("S04.Q02:R15");
    // LAB-S04.Q02:R15-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] SortPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R16-BEGIN
    base::lab::missing_algorithm("S04.Q02:R16");
    // LAB-S04.Q02:R16-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] LimitPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R17-BEGIN
    base::lab::missing_algorithm("S04.Q02:R17");
    // LAB-S04.Q02:R17-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] TopNPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R18-BEGIN
    base::lab::missing_algorithm("S04.Q02:R18");
    // LAB-S04.Q02:R18-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] AppendPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R19-BEGIN
    base::lab::missing_algorithm("S04.Q02:R19");
    // LAB-S04.Q02:R19-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] UniquePlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R20-BEGIN
    base::lab::missing_algorithm("S04.Q02:R20");
    // LAB-S04.Q02:R20-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] AggregatePlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R21-BEGIN
    base::lab::missing_algorithm("S04.Q02:R21");
    // LAB-S04.Q02:R21-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] NestedLoopPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R22-BEGIN
    base::lab::missing_algorithm("S04.Q02:R22");
    // LAB-S04.Q02:R22-END
}

[[maybe_unused]] void propagate_equi_join_required_columns(
    [[maybe_unused]] PlanPtr& outer,
    [[maybe_unused]] PlanPtr& inner,
    [[maybe_unused]] const std::vector<PlanJoinColumnPair>& join_columns,
    [[maybe_unused]] const PlanExpressionPtr& residual_filter,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R23-BEGIN
    base::lab::missing_algorithm("S04.Q02:R23");
    // LAB-S04.Q02:R23-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] MergeJoinPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R24-BEGIN
    base::lab::missing_algorithm("S04.Q02:R24");
    // LAB-S04.Q02:R24-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] HashJoinPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R25-BEGIN
    base::lab::missing_algorithm("S04.Q02:R25");
    // LAB-S04.Q02:R25-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] SubqueryScanPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R26-BEGIN
    base::lab::missing_algorithm("S04.Q02:R26");
    // LAB-S04.Q02:R26-END
}

[[maybe_unused]] void propagate_node_required_columns(
    [[maybe_unused]] SubPlanEvalPlan& node,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R27-BEGIN
    base::lab::missing_algorithm("S04.Q02:R27");
    // LAB-S04.Q02:R27-END
}

[[maybe_unused]] void propagate_required_columns(
    [[maybe_unused]] Plan& plan,
    [[maybe_unused]] const RequiredColumns& required
) {
    // LAB-S04.Q02:R28-BEGIN
    base::lab::missing_algorithm("S04.Q02:R28");
    // LAB-S04.Q02:R28-END
}
}  // namespace

[[maybe_unused]] void set_required_columns([[maybe_unused]] Plan& root) {
    // LAB-S04.Q02:R29-BEGIN
    base::lab::missing_algorithm("S04.Q02:R29");
    // LAB-S04.Q02:R29-END
}
}  // namespace db::planner::detail
