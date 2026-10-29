#pragma once

#include <optional>
#include <vector>

#include "db/base/result.hpp"
#include "db/planner/plan.hpp"

namespace db::planner::detail {

// Validates the stable value descriptors embedded in physical operators. This
// boundary deliberately knows nothing about operator-tree traversal or
// expression recursion, so both policies retain a single owner.
auto valid_type(types::DataType data_type) -> bool;
auto valid_column_index(PlanColumnIndex index, const PlanSchema& schema) -> bool;
auto namespace_matches(
    const PlanParameterReference& reference,
    const ParameterNamespace* parameters
) -> bool;
auto validate_schema(const PlanSchema& schema) -> base::Status;
auto validate_relation(const PlanRelation& relation) -> base::Status;
auto validate_index(const PlanIndex& index, const PlanRelation& relation) -> base::Status;
auto validate_required_columns(
    const std::optional<std::vector<PlanColumnIndex>>& required_columns,
    const PlanSchema& relation_schema
) -> base::Status;

}  // namespace db::planner::detail
