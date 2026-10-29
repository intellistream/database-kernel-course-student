#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/planner/plan.hpp"

#include "plan_descriptor_validation.hpp"
#include "plan_expression_validation.hpp"

namespace db::planner {
namespace {

using detail::finish_subplan_reference_validation;
using detail::namespace_matches;
using detail::SubplanReferenceValidation;
using detail::valid_column_index;
using detail::valid_type;
using detail::validate_expression_list;
using detail::validate_expression_pointer;
using detail::validate_index;
using detail::validate_optional_expression;
using detail::validate_relation;
using detail::validate_required_columns;
using detail::validate_schema;

[[maybe_unused]] auto plan_error(std::string message) -> base::Error {
    return {base::ErrorCode::kPlan, std::move(message)};
}

[[maybe_unused]] auto is_integer([[maybe_unused]] const types::DataType data_type) noexcept
    -> bool {
    // LAB-S04.Q08:R46-BEGIN
    base::lab::missing_algorithm("S04.Q08:R46");
    // LAB-S04.Q08:R46-END
}

[[maybe_unused]] auto
is_valid_bound_comparison([[maybe_unused]] const types::BinaryOperator operation) noexcept -> bool {
    // LAB-S04.Q08:R47-BEGIN
    base::lab::missing_algorithm("S04.Q08:R47");
    // LAB-S04.Q08:R47-END
}

[[maybe_unused]] auto
valid_sort_direction([[maybe_unused]] const PlanSortDirection direction) noexcept -> bool {
    // LAB-S04.Q08:R48-BEGIN
    base::lab::missing_algorithm("S04.Q08:R48");
    // LAB-S04.Q08:R48-END
}

[[maybe_unused]] auto valid_null_order([[maybe_unused]] const PlanNullOrder order) noexcept
    -> bool {
    // LAB-S04.Q08:R49-BEGIN
    base::lab::missing_algorithm("S04.Q08:R49");
    // LAB-S04.Q08:R49-END
}

template <typename... Visitors>
struct Overloaded final : Visitors... {
    using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

[[maybe_unused]] auto validate_metadata([[maybe_unused]] const PlanOperatorMetadata& metadata)
    -> base::Status {
    // LAB-S04.Q08:R50-BEGIN
    base::lab::missing_algorithm("S04.Q08:R50");
    // LAB-S04.Q08:R50-END
}

template <typename Mapping, typename ReferenceOf>
[[maybe_unused]] auto validate_unique_parameter_ids(
    [[maybe_unused]] const std::vector<Mapping>& mappings,
    [[maybe_unused]] const std::string_view context,
    [[maybe_unused]] ReferenceOf reference_of
) -> base::Status {
    // LAB-S04.Q08:R51-BEGIN
    base::lab::missing_algorithm("S04.Q08:R51");
    // LAB-S04.Q08:R51-END
}

[[maybe_unused]] auto validate_rows(
    [[maybe_unused]] const std::vector<std::vector<PlanExpressionPtr>>& rows,
    [[maybe_unused]] const PlanSchema& output,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] const std::string_view context
) -> base::Status {
    // LAB-S04.Q08:R52-BEGIN
    base::lab::missing_algorithm("S04.Q08:R52");
    // LAB-S04.Q08:R52-END
}

auto validate_plan_impl(const Plan& plan, const ParameterNamespace* parameters) -> base::Status;

[[maybe_unused]] auto validate_child(
    [[maybe_unused]] const PlanPtr& child,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] const std::string_view context
) -> base::Status {
    // LAB-S04.Q08:R53-BEGIN
    base::lab::missing_algorithm("S04.Q08:R53");
    // LAB-S04.Q08:R53-END
}

[[maybe_unused]] auto validate_filter(
    [[maybe_unused]] const PlanExpressionPtr& filter,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] const std::string_view context,
    [[maybe_unused]] SubplanReferenceValidation* const subplan_references = nullptr
) -> base::Status {
    // LAB-S04.Q08:R54-BEGIN
    base::lab::missing_algorithm("S04.Q08:R54");
    // LAB-S04.Q08:R54-END
}

[[maybe_unused]] auto
join_schema([[maybe_unused]] const PlanSchema& outer, [[maybe_unused]] const PlanSchema& inner)
    -> PlanSchema {
    // LAB-S04.Q08:R55-BEGIN
    base::lab::missing_algorithm("S04.Q08:R55");
    // LAB-S04.Q08:R55-END
}

[[maybe_unused]] auto validate_sort_items(
    [[maybe_unused]] const std::vector<PlanSortItem>& items,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] const std::string_view context
) -> base::Status {
    // LAB-S04.Q08:R56-BEGIN
    base::lab::missing_algorithm("S04.Q08:R56");
    // LAB-S04.Q08:R56-END
}

[[maybe_unused]] auto validate_bound(
    [[maybe_unused]] const PlanIndexBound& bound,
    [[maybe_unused]] const PlanIndex& index,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R57-BEGIN
    base::lab::missing_algorithm("S04.Q08:R57");
    // LAB-S04.Q08:R57-END
}
enum class BoundSide : std::uint8_t {
    kEquality,
    kLower,
    kUpper,
};

[[maybe_unused]] auto bound_side([[maybe_unused]] const types::BinaryOperator comparison) noexcept
    -> BoundSide {
    // LAB-S04.Q08:R58-BEGIN
    base::lab::missing_algorithm("S04.Q08:R58");
    // LAB-S04.Q08:R58-END
}

struct BoundShapeState final {
    std::size_t current_key{0U};
    bool saw_range{false};
    bool saw_equality{false};
    bool saw_lower{false};
    bool saw_upper{false};
};

[[maybe_unused]] auto advance_bound_key(
    [[maybe_unused]] BoundShapeState& state,
    [[maybe_unused]] const std::size_t key,
    [[maybe_unused]] const std::size_t key_count
) -> base::Status {
    // LAB-S04.Q08:R59-BEGIN
    base::lab::missing_algorithm("S04.Q08:R59");
    // LAB-S04.Q08:R59-END
}

[[maybe_unused]] auto record_equality_bound([[maybe_unused]] BoundShapeState& state)
    -> base::Status {
    // LAB-S04.Q08:R60-BEGIN
    base::lab::missing_algorithm("S04.Q08:R60");
    // LAB-S04.Q08:R60-END
}

[[maybe_unused]] auto
record_range_bound([[maybe_unused]] BoundShapeState& state, [[maybe_unused]] const BoundSide side)
    -> base::Status {
    // LAB-S04.Q08:R61-BEGIN
    base::lab::missing_algorithm("S04.Q08:R61");
    // LAB-S04.Q08:R61-END
}

[[maybe_unused]] auto validate_bound_shape(
    [[maybe_unused]] const std::vector<PlanIndexBound>& bounds,
    [[maybe_unused]] const std::size_t key_count
) -> base::Status {
    // LAB-S04.Q08:R62-BEGIN
    base::lab::missing_algorithm("S04.Q08:R62");
    // LAB-S04.Q08:R62-END
}

[[maybe_unused]] auto modification_value_schema([[maybe_unused]] const ModifyTablePlan& modify)
    -> base::Result<PlanSchema> {
    // LAB-S04.Q08:R63-BEGIN
    base::lab::missing_algorithm("S04.Q08:R63");
    // LAB-S04.Q08:R63-END
}

[[maybe_unused]] auto validate_update_assignments(
    [[maybe_unused]] const ModifyTablePlan& modify,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R64-BEGIN
    base::lab::missing_algorithm("S04.Q08:R64");
    // LAB-S04.Q08:R64-END
}

[[maybe_unused]] auto validate_insert_source(
    [[maybe_unused]] const ModifyTablePlan& modify,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R65-BEGIN
    base::lab::missing_algorithm("S04.Q08:R65");
    // LAB-S04.Q08:R65-END
}

[[maybe_unused]] auto validate_modify_source(
    [[maybe_unused]] const ModifyTablePlan& modify,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R66-BEGIN
    base::lab::missing_algorithm("S04.Q08:R66");
    // LAB-S04.Q08:R66-END
}

[[maybe_unused]] auto validate_aggregate_items(
    [[maybe_unused]] const AggregatePlan& aggregate,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R67-BEGIN
    base::lab::missing_algorithm("S04.Q08:R67");
    // LAB-S04.Q08:R67-END
}

[[maybe_unused]] auto validate_aggregate_plan(
    [[maybe_unused]] const AggregatePlan& aggregate,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R68-BEGIN
    base::lab::missing_algorithm("S04.Q08:R68");
    // LAB-S04.Q08:R68-END
}

[[maybe_unused]] auto validate_values_plan(
    [[maybe_unused]] const ValuesPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R69-BEGIN
    base::lab::missing_algorithm("S04.Q08:R69");
    // LAB-S04.Q08:R69-END
}

[[maybe_unused]] auto validate_seq_scan_plan(
    [[maybe_unused]] const SeqScanPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R70-BEGIN
    base::lab::missing_algorithm("S04.Q08:R70");
    // LAB-S04.Q08:R70-END
}

[[maybe_unused]] auto validate_index_scan_plan(
    [[maybe_unused]] const IndexScanPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R71-BEGIN
    base::lab::missing_algorithm("S04.Q08:R71");
    // LAB-S04.Q08:R71-END
}

[[maybe_unused]] auto validate_result_plan(
    [[maybe_unused]] const ResultPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R72-BEGIN
    base::lab::missing_algorithm("S04.Q08:R72");
    // LAB-S04.Q08:R72-END
}

[[maybe_unused]] auto validate_modify_plan(
    [[maybe_unused]] const ModifyTablePlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R73-BEGIN
    base::lab::missing_algorithm("S04.Q08:R73");
    // LAB-S04.Q08:R73-END
}

[[maybe_unused]] auto validate_passthrough_plan(
    [[maybe_unused]] const PlanOperatorMetadata& metadata,
    [[maybe_unused]] const PlanPtr& input,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] const std::string_view context
) -> base::Status {
    // LAB-S04.Q08:R74-BEGIN
    base::lab::missing_algorithm("S04.Q08:R74");
    // LAB-S04.Q08:R74-END
}

[[maybe_unused]] auto validate_sort_plan(
    [[maybe_unused]] const SortPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R75-BEGIN
    base::lab::missing_algorithm("S04.Q08:R75");
    // LAB-S04.Q08:R75-END
}

[[maybe_unused]] auto validate_limit_expression(
    [[maybe_unused]] const PlanExpressionPtr& expression,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R76-BEGIN
    base::lab::missing_algorithm("S04.Q08:R76");
    // LAB-S04.Q08:R76-END
}

[[maybe_unused]] auto validate_limit_plan(
    [[maybe_unused]] const LimitPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R77-BEGIN
    base::lab::missing_algorithm("S04.Q08:R77");
    // LAB-S04.Q08:R77-END
}

[[maybe_unused]] auto validate_top_n_plan(
    [[maybe_unused]] const TopNPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R78-BEGIN
    base::lab::missing_algorithm("S04.Q08:R78");
    // LAB-S04.Q08:R78-END
}

[[maybe_unused]] auto validate_unique_plan(
    [[maybe_unused]] const UniquePlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R79-BEGIN
    base::lab::missing_algorithm("S04.Q08:R79");
    // LAB-S04.Q08:R79-END
}

[[maybe_unused]] auto validate_nested_loop_plan(
    [[maybe_unused]] const NestedLoopPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R80-BEGIN
    base::lab::missing_algorithm("S04.Q08:R80");
    // LAB-S04.Q08:R80-END
}

template <typename JoinPlan>
[[maybe_unused]] auto validate_equi_join_plan(
    [[maybe_unused]] const JoinPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] const std::string_view context
) -> base::Status {
    // LAB-S04.Q08:R81-BEGIN
    base::lab::missing_algorithm("S04.Q08:R81");
    // LAB-S04.Q08:R81-END
}

[[maybe_unused]] auto validate_subquery_scan_plan(
    [[maybe_unused]] const SubqueryScanPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R82-BEGIN
    base::lab::missing_algorithm("S04.Q08:R82");
    // LAB-S04.Q08:R82-END
}

[[maybe_unused]] auto validate_subplan_input_and_arguments(
    [[maybe_unused]] const PlanSubPlan& subplan,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] const std::size_t preceding_subplan_count
) -> base::Status {
    // LAB-S04.Q08:R83-BEGIN
    base::lab::missing_algorithm("S04.Q08:R83");
    // LAB-S04.Q08:R83-END
}

[[maybe_unused]] auto validate_subplan_parameter_mappings(
    [[maybe_unused]] const PlanSubPlan& subplan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R84-BEGIN
    base::lab::missing_algorithm("S04.Q08:R84");
    // LAB-S04.Q08:R84-END
}

[[maybe_unused]] auto validate_subplan_strategy([[maybe_unused]] const PlanSubPlan& subplan)
    -> base::Status {
    // LAB-S04.Q08:R85-BEGIN
    base::lab::missing_algorithm("S04.Q08:R85");
    // LAB-S04.Q08:R85-END
}

[[maybe_unused]] auto validate_subplan(
    [[maybe_unused]] const PlanSubPlan& subplan,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] const std::size_t preceding_subplan_count
) -> base::Status {
    // LAB-S04.Q08:R86-BEGIN
    base::lab::missing_algorithm("S04.Q08:R86");
    // LAB-S04.Q08:R86-END
}

[[maybe_unused]] auto validate_subplan_eval_plan(
    [[maybe_unused]] const SubPlanEvalPlan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R87-BEGIN
    base::lab::missing_algorithm("S04.Q08:R87");
    // LAB-S04.Q08:R87-END
}

[[maybe_unused]] auto validate_plan_impl(
    [[maybe_unused]] const Plan& plan,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> base::Status {
    // LAB-S04.Q08:R88-BEGIN
    base::lab::missing_algorithm("S04.Q08:R88");
    // LAB-S04.Q08:R88-END
}
}  // namespace

[[maybe_unused]] auto validate_plan([[maybe_unused]] const Plan& plan) -> base::Status {
    // LAB-S04.Q08:R89-BEGIN
    base::lab::missing_algorithm("S04.Q08:R89");
    // LAB-S04.Q08:R89-END
}

[[maybe_unused]] auto validate_plan(
    [[maybe_unused]] const Plan& plan,
    [[maybe_unused]] const ParameterNamespace& parameters
) -> base::Status {
    // LAB-S04.Q08:R90-BEGIN
    base::lab::missing_algorithm("S04.Q08:R90");
    // LAB-S04.Q08:R90-END
}

[[maybe_unused]] auto validate_planned_statement([[maybe_unused]] const PlannedStatement& statement)
    -> base::Status {
    // LAB-S04.Q08:R91-BEGIN
    base::lab::missing_algorithm("S04.Q08:R91");
    // LAB-S04.Q08:R91-END
}
}  // namespace db::planner
