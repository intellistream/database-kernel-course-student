#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/planner/plan.hpp"

namespace db::planner::detail {

// One owning SubPlanEval scope supplies the number of references visible to
// an expression. Structural validation records range failures so its caller
// can preserve existing structural/type error priority before reporting them.
struct SubplanReferenceValidation final {
    std::size_t visible_subplan_count{0U};
    bool outside_visible_range{false};
};

// Recursively validates physical expressions against their immediate input
// schema and optional statement-wide parameter namespace. Operator ownership,
// child-plan traversal, and publication remain outside this boundary.
auto validate_expression_pointer(
    const PlanExpressionPtr& expression,
    const PlanSchema& input_schema,
    const ParameterNamespace* parameters,
    std::string_view context,
    SubplanReferenceValidation* subplan_references = nullptr
) -> base::Status;
auto validate_optional_expression(
    const PlanExpressionPtr& expression,
    const PlanSchema& input_schema,
    const ParameterNamespace* parameters,
    SubplanReferenceValidation* subplan_references = nullptr
) -> base::Status;
auto validate_expression_list(
    const std::vector<PlanExpressionPtr>& expressions,
    const PlanSchema& input_schema,
    const ParameterNamespace* parameters,
    std::string_view context,
    std::size_t minimum,
    SubplanReferenceValidation* subplan_references = nullptr
) -> base::Status;
auto finish_subplan_reference_validation(const SubplanReferenceValidation& validation)
    -> base::Status;

}  // namespace db::planner::detail
