#include "plan_descriptor_validation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/type_registry.hpp"

namespace db::planner::detail {
namespace {

[[maybe_unused]] auto descriptor_error(std::string message) -> base::Error {
    return {base::ErrorCode::kPlan, std::move(message)};
}

}  // namespace

[[maybe_unused]] auto valid_type([[maybe_unused]] const types::DataType data_type) -> bool {
    // LAB-S04.Q08:R18-BEGIN
    base::lab::missing_algorithm("S04.Q08:R18");
    // LAB-S04.Q08:R18-END
}

[[maybe_unused]] auto valid_column_index(
    [[maybe_unused]] const PlanColumnIndex index,
    [[maybe_unused]] const PlanSchema& schema
) -> bool {
    // LAB-S04.Q08:R19-BEGIN
    base::lab::missing_algorithm("S04.Q08:R19");
    // LAB-S04.Q08:R19-END
}

[[maybe_unused]] auto namespace_matches(
    [[maybe_unused]] const PlanParameterReference& reference,
    [[maybe_unused]] const ParameterNamespace* const parameters
) -> bool {
    // LAB-S04.Q08:R20-BEGIN
    base::lab::missing_algorithm("S04.Q08:R20");
    // LAB-S04.Q08:R20-END
}

[[maybe_unused]] auto validate_schema([[maybe_unused]] const PlanSchema& schema) -> base::Status {
    // LAB-S04.Q08:R21-BEGIN
    base::lab::missing_algorithm("S04.Q08:R21");
    // LAB-S04.Q08:R21-END
}

[[maybe_unused]] auto validate_relation([[maybe_unused]] const PlanRelation& relation)
    -> base::Status {
    // LAB-S04.Q08:R22-BEGIN
    base::lab::missing_algorithm("S04.Q08:R22");
    // LAB-S04.Q08:R22-END
}

[[maybe_unused]] auto validate_index(
    [[maybe_unused]] const PlanIndex& index,
    [[maybe_unused]] const PlanRelation& relation
) -> base::Status {
    // LAB-S04.Q08:R23-BEGIN
    base::lab::missing_algorithm("S04.Q08:R23");
    // LAB-S04.Q08:R23-END
}

[[maybe_unused]] auto validate_required_columns(
    [[maybe_unused]] const std::optional<std::vector<PlanColumnIndex>>& required_columns,
    [[maybe_unused]] const PlanSchema& relation_schema
) -> base::Status {
    // LAB-S04.Q08:R24-BEGIN
    base::lab::missing_algorithm("S04.Q08:R24");
    // LAB-S04.Q08:R24-END
}
}  // namespace db::planner::detail
