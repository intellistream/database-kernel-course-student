#include "query_executor.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/lab_missing.hpp"
#include "db/execution/exec_context.hpp"
#include "db/planner/explain.hpp"
#include "db/planner/plan.hpp"

#include "dispatch_internal.hpp"
#include "logical_scan_ssi.hpp"
#include "materialized_result_receiver.hpp"
#include "query_error.hpp"
#include "storage_plan_factory.hpp"

namespace db::engine::detail {
namespace {

[[maybe_unused]] auto close_plan_after_failure(
    execution::PlanState& state,
    execution::ExecContext& context,
    base::Error primary_error,
    const std::string_view operation
) -> base::Error {
    auto closed = state.close(context);
    if (!closed) {
        return std::move(primary_error)
            .with_context(
                "engine.query",
                std::string{operation} + " (plan cleanup also failed: " + closed.error().to_string()
                    + ")"
            );
    }
    return primary_error;
}

[[maybe_unused]] auto result_schema_for(const planner::Plan& plan) -> base::Result<ResultSchema> {
    std::vector<ResultColumn> columns;
    const auto& metadata = planner::plan_metadata(plan);
    columns.reserve(metadata.output.columns.size());
    for (const auto& column : metadata.output.columns) {
        columns.push_back(
            ResultColumn{
                .name = column.name,
                .data_type = column.data_type,
            }
        );
    }
    return ResultSchema::create(std::move(columns));
}

[[maybe_unused]] auto result_receiver_for(
    [[maybe_unused]] const DispatchStatementKind kind,
    [[maybe_unused]] const planner::Plan& root
) -> base::Result<std::optional<MaterializedResultReceiver>> {
    // LAB-S02.Q11:R43-BEGIN
    base::lab::missing_algorithm("S02.Q11:R43");
    // LAB-S02.Q11:R43-END
}

[[maybe_unused]] auto explain_rows([[maybe_unused]] const std::string_view rendered)
    -> std::vector<std::vector<types::Value>> {
    // LAB-S02.Q11:R44-BEGIN
    base::lab::missing_algorithm("S02.Q11:R44");
    // LAB-S02.Q11:R44-END
}
}  // namespace

[[maybe_unused]] auto StorageExecutionPort::execute(
    [[maybe_unused]] const DispatchPlan& plan,
    [[maybe_unused]] const std::span<const types::Value> parameter_values
) -> base::Result<DispatchExecutionResult> {
    // LAB-S02.Q11:R45-BEGIN
    base::lab::missing_algorithm("S02.Q11:R45");
    // LAB-S02.Q11:R45-END
}
}  // namespace db::engine::detail
