#include "db/execution/nested_loop.hpp"

#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "join_support.hpp"
#include "state_support.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto nested_loop_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.nested_loop", "construct");
}

[[maybe_unused]] auto nested_loop_runtime_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kExecution, std::move(message)}.with_context(
        "execution.nested_loop",
        "run"
    );
}

}  // namespace

NestedLoopState::NestedLoopState(
    TupleSlot output_slot,
    std::unique_ptr<PlanState> outer,
    std::unique_ptr<PlanState> inner,
    std::optional<ExpressionProgram> join_filter,
    std::vector<ParameterBinding> parameters
)
    : PlanState{std::move(output_slot)},
      outer_{std::move(outer)},
      inner_{std::move(inner)},
      join_filter_{std::move(join_filter)},
      parameters_{std::move(parameters)} {}

[[maybe_unused]] auto NestedLoopState::create(
    [[maybe_unused]] const planner::NestedLoopPlan& plan,
    [[maybe_unused]] std::unique_ptr<PlanState> outer,
    [[maybe_unused]] std::unique_ptr<PlanState> inner
) -> base::Result<std::unique_ptr<NestedLoopState>> {
    // LAB-S07.Q02:R01-BEGIN
    base::lab::missing_algorithm("S07.Q02:R01");
    // LAB-S07.Q02:R01-END
}

[[maybe_unused]] auto NestedLoopState::bind_outer_parameters(
    [[maybe_unused]] const std::span<const types::Value> outer_values,
    [[maybe_unused]] ExecContext& context
) const -> base::Status {
    // LAB-S07.Q05:R01-BEGIN
    base::lab::missing_algorithm("S07.Q05:R01");
    // LAB-S07.Q05:R01-END
}

[[maybe_unused]] auto NestedLoopState::clear_parameters([[maybe_unused]] ExecContext& context) const
    -> base::Status {
    // LAB-S07.Q05:R02-BEGIN
    base::lab::missing_algorithm("S07.Q05:R02");
    // LAB-S07.Q05:R02-END
}

[[maybe_unused]] auto NestedLoopState::prepare_inner([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S07.Q05:R03-BEGIN
    base::lab::missing_algorithm("S07.Q05:R03");
    // LAB-S07.Q05:R03-END
}

[[maybe_unused]] auto NestedLoopState::advance_outer([[maybe_unused]] ExecContext& context)
    -> base::Result<bool> {
    // LAB-S07.Q02:R02-BEGIN
    base::lab::missing_algorithm("S07.Q02:R02");
    // LAB-S07.Q02:R02-END
}

[[maybe_unused]] auto NestedLoopState::do_open([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S07.Q02:R03-BEGIN
    base::lab::missing_algorithm("S07.Q02:R03");
    // LAB-S07.Q02:R03-END
}

[[maybe_unused]] auto NestedLoopState::do_next([[maybe_unused]] ExecContext& context)
    -> base::Result<bool> {
    // LAB-S07.Q02:R04-BEGIN
    base::lab::missing_algorithm("S07.Q02:R04");
    // LAB-S07.Q02:R04-END
}

[[maybe_unused]] auto NestedLoopState::process_inner([[maybe_unused]] ExecContext& context)
    -> base::Result<std::optional<bool>> {
    // LAB-S07.Q02:R05-BEGIN
    base::lab::missing_algorithm("S07.Q02:R05");
    // LAB-S07.Q02:R05-END
}

[[maybe_unused]] auto NestedLoopState::do_rescan([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S07.Q05:R04-BEGIN
    base::lab::missing_algorithm("S07.Q05:R04");
    // LAB-S07.Q05:R04-END
}

auto NestedLoopState::do_close(ExecContext& context) -> base::Status {
    outer_values_.clear();
    have_outer_ = false;
    inner_opened_ = false;
    auto inner_closed = inner_->close(context);
    auto outer_closed = outer_->close(context);
    auto cleared = clear_parameters(context);
    if (!inner_closed) {
        return base::fail(std::move(inner_closed).error());
    }
    if (!outer_closed) {
        return base::fail(std::move(outer_closed).error());
    }
    if (!cleared) {
        return base::fail(std::move(cleared).error());
    }
    return {};
}

}  // namespace db::execution
