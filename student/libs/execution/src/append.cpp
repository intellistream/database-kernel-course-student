#include "db/execution/append.hpp"

#include "db/base/error.hpp"

#include "state_support.hpp"

namespace db::execution {
auto AppendState::create(
    const planner::AppendPlan& plan,
    std::vector<std::unique_ptr<PlanState>> inputs
) -> base::Result<std::unique_ptr<AppendState>> {
    if (inputs.empty() || inputs.size() != plan.inputs.size()) {
        return base::fail(base::Error{base::ErrorCode::kPlan, "append input count mismatch"});
    }
    auto slot = detail::output_slot_for_schema(plan.metadata.output);
    if (!slot) {
        return base::fail(std::move(slot).error());
    }
    for (const auto& input : inputs) {
        if (!input) {
            return base::fail(base::Error{base::ErrorCode::kPlan, "append input absent"});
        }
        auto valid = detail::validate_passthrough_shape(
            input->output_slot().descriptor(),
            slot->descriptor(),
            "append"
        );
        if (!valid) {
            return base::fail(std::move(valid).error());
        }
    }
    return std::unique_ptr<AppendState>{
        new AppendState{std::move(slot).value(), std::move(inputs)}
    };
}

// LAB-S06.Q02:R01-BEGIN
[[maybe_unused]] auto AppendState::do_open([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S06.Q02:R01: initialize append stream"}
    );
}

// LAB-S06.Q02:R01-END

// LAB-S06.Q02:R02-BEGIN
[[maybe_unused]] auto AppendState::do_next([[maybe_unused]] ExecContext&) -> base::Result<bool> {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S06.Q02:R02: stream append inputs lazily"}
    );
}

// LAB-S06.Q02:R02-END

// LAB-S06.Q02:R03-BEGIN
[[maybe_unused]] auto AppendState::do_rescan([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S06.Q02:R03: rescan append inputs"}
    );
}

// LAB-S06.Q02:R03-END

auto AppendState::do_close(ExecContext& context) -> base::Status {
    base::Status result;
    for (auto& input : inputs_) {
        auto status = input->close(context);
        if (!status && result) {
            result = std::move(status);
        }
    }
    return result;
}

}  // namespace db::execution
