#include "db/execution/subquery_scan.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "state_support.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto subquery_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "execution.subquery_scan",
        "construct"
    );
}

}  // namespace

SubqueryScanState::SubqueryScanState(
    TupleSlot output_slot,
    std::unique_ptr<PlanState> input,
    std::optional<ExpressionProgram> filter
)
    : PlanState{std::move(output_slot)},
      input_{std::move(input)},
      filter_{std::move(filter)} {}

[[maybe_unused]] auto SubqueryScanState::create(
    [[maybe_unused]] const planner::SubqueryScanPlan& plan,
    [[maybe_unused]] std::unique_ptr<PlanState> input
) -> base::Result<std::unique_ptr<SubqueryScanState>> {
    // LAB-S06.Q05:R01-BEGIN
    base::lab::missing_algorithm("S06.Q05:R01");
    // LAB-S06.Q05:R01-END
}

[[maybe_unused]] auto SubqueryScanState::do_open([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S06.Q05:R02-BEGIN
    base::lab::missing_algorithm("S06.Q05:R02");
    // LAB-S06.Q05:R02-END
}

[[maybe_unused]] auto SubqueryScanState::do_next([[maybe_unused]] ExecContext& context)
    -> base::Result<bool> {
    // LAB-S06.Q05:R03-BEGIN
    base::lab::missing_algorithm("S06.Q05:R03");
    // LAB-S06.Q05:R03-END
}

[[maybe_unused]] auto SubqueryScanState::do_rescan([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S06.Q05:R04-BEGIN
    base::lab::missing_algorithm("S06.Q05:R04");
    // LAB-S06.Q05:R04-END
}

auto SubqueryScanState::do_close(ExecContext& context) -> base::Status {
    return input_->close(context);
}

}  // namespace db::execution
