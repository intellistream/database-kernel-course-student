#include "projection_program.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto projection_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kPlan, std::move(message)}.with_context(
        "execution.operator",
        "construct"
    );
}

[[maybe_unused]] auto direct_input_column(
    [[maybe_unused]] const planner::PlanProjectionItem& item,
    [[maybe_unused]] const std::span<const TupleColumn> input_columns
) noexcept -> std::optional<std::size_t> {
    // LAB-S02.Q07:R09-BEGIN
    base::lab::missing_algorithm("S02.Q07:R09");
    // LAB-S02.Q07:R09-END
}

void record_input_references(
    const planner::PlanExpression& expression,
    std::size_t output_index,
    std::span<std::optional<std::size_t>> last_references
);

[[maybe_unused]] void record_child_references(
    [[maybe_unused]] const planner::PlanExpressionPtr& expression,
    [[maybe_unused]] const std::size_t output_index,
    [[maybe_unused]] const std::span<std::optional<std::size_t>> last_references
) {
    // LAB-S02.Q07:R11-BEGIN
    base::lab::missing_algorithm("S02.Q07:R11");
    // LAB-S02.Q07:R11-END
}

[[maybe_unused]] void record_input_references(
    [[maybe_unused]] const planner::PlanExpression& expression,
    [[maybe_unused]] const std::size_t output_index,
    [[maybe_unused]] const std::span<std::optional<std::size_t>> last_references
) {
    // LAB-S02.Q07:R10-BEGIN
    base::lab::missing_algorithm("S02.Q07:R10");
    // LAB-S02.Q07:R10-END
}

}  // namespace

ProjectionProgram::ProjectionProgram(
    std::vector<Step> steps,
    std::vector<ExpressionProgram> programs,
    const bool identity
)
    : steps_{std::move(steps)},
      programs_{std::move(programs)},
      identity_{identity} {}

[[maybe_unused]] auto ProjectionProgram::compile(
    [[maybe_unused]] const planner::ResultPlan& plan,
    [[maybe_unused]] const std::span<const TupleColumn> input_columns
) -> base::Result<std::unique_ptr<ProjectionProgram>> {
    // LAB-S02.Q07:R12-BEGIN
    base::lab::missing_algorithm("S02.Q07:R12");
    // LAB-S02.Q07:R12-END
}

[[maybe_unused]] auto ProjectionProgram::identity() const noexcept -> bool {
    // LAB-S02.Q07:R13-BEGIN
    base::lab::missing_algorithm("S02.Q07:R13");
    // LAB-S02.Q07:R13-END
}

[[maybe_unused]] auto ProjectionProgram::evaluate(
    [[maybe_unused]] const Tuple& input,
    [[maybe_unused]] const ExecContext& context,
    [[maybe_unused]] std::vector<types::Value> recycled_values
) const -> base::Result<std::vector<types::Value>> {
    // LAB-S02.Q07:R14-BEGIN
    base::lab::missing_algorithm("S02.Q07:R14");
    // LAB-S02.Q07:R14-END
}

[[maybe_unused]] auto ProjectionProgram::evaluate_movable(
    [[maybe_unused]] Tuple& input,
    [[maybe_unused]] const ExecContext& context,
    [[maybe_unused]] std::vector<types::Value> recycled_values
) const -> base::Result<std::vector<types::Value>> {
    // LAB-S02.Q07:R15-BEGIN
    base::lab::missing_algorithm("S02.Q07:R15");
    // LAB-S02.Q07:R15-END
}

[[maybe_unused]] auto ProjectionProgram::evaluate_impl(
    [[maybe_unused]] const Tuple& input,
    [[maybe_unused]] std::vector<types::Value>* const movable_input_values,
    [[maybe_unused]] const ExecContext& context,
    [[maybe_unused]] std::vector<types::Value> recycled_values
) const -> base::Result<std::vector<types::Value>> {
    // LAB-S02.Q07:R16-BEGIN
    base::lab::missing_algorithm("S02.Q07:R16");
    // LAB-S02.Q07:R16-END
}

}  // namespace db::execution
