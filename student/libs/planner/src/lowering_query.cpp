#include <memory>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "lowering_internal.hpp"

namespace db::planner::detail {

[[maybe_unused]] auto append_subplan_filter(
    [[maybe_unused]] Plan input,
    [[maybe_unused]] const InputLayout& input_layout,
    [[maybe_unused]] PreprocessedExpression& filter,
    [[maybe_unused]] LoweringContext& context,
    [[maybe_unused]] const std::string& subject
) -> base::Result<Plan> {
    // LAB-S02.Q05:R48-BEGIN
    base::lab::missing_algorithm("S02.Q05:R48");
    // LAB-S02.Q05:R48-END
}

[[maybe_unused]] auto lower_query_plan(
    [[maybe_unused]] PreprocessedQuery& query,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<Plan> {
    // LAB-S02.Q05:R49-BEGIN
    base::lab::missing_algorithm("S02.Q05:R49");
    // LAB-S02.Q05:R49-END
}
}  // namespace db::planner::detail

namespace db::planner {

[[maybe_unused]] auto lower_preprocessed_select(
    [[maybe_unused]] PreprocessedQuery query,
    [[maybe_unused]] const MetadataPort& metadata_port,
    [[maybe_unused]] ParameterNamespace parameters
) -> base::Result<LoweredSelectPlan> {
    // LAB-S02.Q05:R50-BEGIN
    base::lab::missing_algorithm("S02.Q05:R50");
    // LAB-S02.Q05:R50-END
}
}  // namespace db::planner
