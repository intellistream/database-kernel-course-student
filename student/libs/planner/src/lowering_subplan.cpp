#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "db/base/lab_missing.hpp"

#include "lowering_internal.hpp"

namespace db::planner::detail {

[[maybe_unused]] auto materialize_expression_subplan(
    [[maybe_unused]] PreprocessedQuery& query,
    [[maybe_unused]] const PlanSubPlanStrategy strategy,
    [[maybe_unused]] const types::DataType result_type,
    [[maybe_unused]] PlanExpressionPtr input_expression,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] LoweringContext& context,
    [[maybe_unused]] std::vector<PlanSubPlan>& subplans
) -> base::Result<PlanSubPlanIndex> {
    // LAB-S07.Q08:R36-BEGIN
    base::lab::missing_algorithm("S07.Q08:R36");
    // LAB-S07.Q08:R36-END
}
}  // namespace db::planner::detail
