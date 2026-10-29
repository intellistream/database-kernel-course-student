#include "db/execution/unique.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "state_support.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto unique_error(const base::ErrorCode code, std::string message) -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.unique", "construct");
}

}  // namespace

// LAB-S06.Q03:R01-BEGIN
[[maybe_unused]] auto UniqueState::ValuesHash::operator()(
    [[maybe_unused]] const std::vector<types::Value>&
) const noexcept -> std::size_t {
    base::lab::missing_algorithm("S06.Q03:R01");
}

// LAB-S06.Q03:R01-END
auto UniqueState::create(const planner::UniquePlan& plan, std::unique_ptr<PlanState> input)
    -> base::Result<std::unique_ptr<UniqueState>> {
    if (input == nullptr) {
        return base::fail(unique_error(base::ErrorCode::kPlan, "unique input is absent"));
    }
    auto slot = detail::output_slot_for_schema(plan.metadata.output);
    if (!slot) {
        return base::fail(std::move(slot).error());
    }
    auto shape = detail::validate_passthrough_shape(
        input->output_slot().descriptor(),
        slot->descriptor(),
        "unique"
    );
    if (!shape) {
        return base::fail(std::move(shape).error());
    }
    return std::unique_ptr<UniqueState>{new UniqueState{
        std::move(slot).value(),
        std::move(input),
    }};
}

// LAB-S06.Q03:R02-BEGIN
[[maybe_unused]] auto UniqueState::do_open([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S06.Q03:R02: initialize whole-row unique state"}
    );
}

// LAB-S06.Q03:R02-END

// LAB-S06.Q03:R03-BEGIN
[[maybe_unused]] auto UniqueState::do_next([[maybe_unused]] ExecContext&) -> base::Result<bool> {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S06.Q03:R03: filter duplicate typed rows"}
    );
}

// LAB-S06.Q03:R03-END

// LAB-S06.Q03:R04-BEGIN
[[maybe_unused]] auto UniqueState::do_rescan([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S06.Q03:R04: reset whole-row unique state"}
    );
}

// LAB-S06.Q03:R04-END

auto UniqueState::do_close(ExecContext& context) -> base::Status {
    seen_.clear();
    return input_->close(context);
}

}  // namespace db::execution
