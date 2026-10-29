#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/lab_missing.hpp"
#include "db/types/type_registry.hpp"

#include "lowering_internal.hpp"

namespace db::planner::detail {
namespace {

struct AggregateRewriteResult final {
    std::vector<PlanProjectionItem> projection;
    PlanExpressionPtr having;
    std::vector<PlanSortItem> order_by;
};

[[maybe_unused]] auto lower_aggregate_kind([[maybe_unused]] const QueryAggregateKind kind) noexcept
    -> PlanAggregateKind {
    // LAB-S05.Q04:R11-BEGIN
    base::lab::missing_algorithm("S05.Q04:R11");
    // LAB-S05.Q04:R11-END
}

[[maybe_unused]] auto default_aggregate_name([[maybe_unused]] const QueryAggregateKind kind)
    -> std::string {
    // LAB-S05.Q04:R12-BEGIN
    base::lab::missing_algorithm("S05.Q04:R12");
    // LAB-S05.Q04:R12-END
}

[[maybe_unused]] auto lower_group_by(
    [[maybe_unused]] const std::vector<PreprocessedExpressionPtr>& groups,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] PlanSchema& output,
    [[maybe_unused]] ParameterNamespace& parameters
) -> base::Result<std::vector<PlanExpressionPtr>> {
    // LAB-S05.Q04:R13-BEGIN
    base::lab::missing_algorithm("S05.Q04:R13");
    // LAB-S05.Q04:R13-END
}

[[maybe_unused]] auto rewrite_projection(
    [[maybe_unused]] const std::vector<PreprocessedProjectionItem>& projection,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] AggregateExpressionContext& context
) -> base::Result<std::vector<PlanProjectionItem>> {
    // LAB-S05.Q04:R14-BEGIN
    base::lab::missing_algorithm("S05.Q04:R14");
    // LAB-S05.Q04:R14-END
}

[[maybe_unused]] auto rewrite_having(
    [[maybe_unused]] const PreprocessedExpressionPtr& having,
    [[maybe_unused]] AggregateExpressionContext& context
) -> base::Result<PlanExpressionPtr> {
    // LAB-S05.Q04:R15-BEGIN
    base::lab::missing_algorithm("S05.Q04:R15");
    // LAB-S05.Q04:R15-END
}

[[maybe_unused]] auto rewrite_order_by(
    [[maybe_unused]] const std::vector<PreprocessedSortItem>& order_by,
    [[maybe_unused]] AggregateExpressionContext& context
) -> base::Result<std::vector<PlanSortItem>> {
    // LAB-S05.Q04:R16-BEGIN
    base::lab::missing_algorithm("S05.Q04:R16");
    // LAB-S05.Q04:R16-END
}

[[maybe_unused]] auto materialize_aggregates(
    [[maybe_unused]] const std::vector<AggregateExpressionRegistration>& registrations,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] ParameterNamespace& parameters
) -> base::Result<std::vector<PlanAggregate>> {
    // LAB-S05.Q04:R17-BEGIN
    base::lab::missing_algorithm("S05.Q04:R17");
    // LAB-S05.Q04:R17-END
}

[[maybe_unused]] auto scan_proves_ordered_minimum(
    [[maybe_unused]] const Plan& input,
    [[maybe_unused]] const PlanAggregate& aggregate
) -> bool {
    // LAB-S05.Q04:R18-BEGIN
    base::lab::missing_algorithm("S05.Q04:R18");
    // LAB-S05.Q04:R18-END
}

[[maybe_unused]] auto rewrite_aggregate_select(
    [[maybe_unused]] const PreprocessedSelectQuery& select,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] AggregateExpressionContext& context
) -> base::Result<AggregateRewriteResult> {
    // LAB-S05.Q04:R19-BEGIN
    base::lab::missing_algorithm("S05.Q04:R19");
    // LAB-S05.Q04:R19-END
}

[[maybe_unused]] auto append_aggregate_order_and_limit(
    [[maybe_unused]] Plan input,
    [[maybe_unused]] const PlanSchema& schema,
    [[maybe_unused]] std::vector<PlanSortItem> order_by,
    [[maybe_unused]] const PreprocessedExpressionPtr& limit,
    [[maybe_unused]] const PreprocessedExpressionPtr& offset,
    [[maybe_unused]] ParameterNamespace& parameters
) -> base::Result<Plan> {
    // LAB-S05.Q04:R20-BEGIN
    base::lab::missing_algorithm("S05.Q04:R20");
    // LAB-S05.Q04:R20-END
}
}  // namespace

[[maybe_unused]] auto lower_aggregate_select_operators(
    [[maybe_unused]] const PreprocessedSelectQuery& select,
    [[maybe_unused]] LoweredPlanInput input,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<Plan> {
    // LAB-S05.Q04:R21-BEGIN
    base::lab::missing_algorithm("S05.Q04:R21");
    // LAB-S05.Q04:R21-END
}
}  // namespace db::planner::detail
