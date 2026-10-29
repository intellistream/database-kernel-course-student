#include "db/execution/sort.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "db/base/error.hpp"

#include "state_support.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto sort_state_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.sort_state", "construct");
}

}  // namespace

SortState::SortState(TupleSlot output_slot, std::unique_ptr<PlanState> input, SortSupport support)
    : PlanState{std::move(output_slot)},
      input_{std::move(input)},
      support_{std::move(support)} {}

auto SortState::create(const planner::SortPlan& plan, std::unique_ptr<PlanState> input)
    -> base::Result<std::unique_ptr<SortState>> {
    if (input == nullptr) {
        return base::fail(sort_state_error(base::ErrorCode::kPlan, "sort input is absent"));
    }
    auto slot = detail::output_slot_for_schema(plan.metadata.output);
    if (!slot) {
        return base::fail(std::move(slot).error());
    }
    auto shape = detail::validate_passthrough_shape(
        input->output_slot().descriptor(),
        slot->descriptor(),
        "sort"
    );
    if (!shape) {
        return base::fail(std::move(shape).error());
    }
    auto support = SortSupport::compile(plan.order_by);
    if (!support) {
        return base::fail(std::move(support).error());
    }
    return std::unique_ptr<SortState>{new SortState{
        std::move(slot).value(),
        std::move(input),
        std::move(support).value(),
    }};
}

// LAB-S02.Q08:R10-BEGIN
[[maybe_unused]] auto SortState::do_open([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R10: initialize materializing sort state",
        }
    );
}

// LAB-S02.Q08:R10-END
// LAB-S02.Q08:R11-BEGIN
[[maybe_unused]] auto SortState::materialize_child([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R11: materialize and order child rows",
        }
    );
}

// LAB-S02.Q08:R11-END
// LAB-S02.Q08:R12-BEGIN
[[maybe_unused]] auto SortState::do_next([[maybe_unused]] ExecContext&) -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R12: emit sorted rows",
        }
    );
}

// LAB-S02.Q08:R12-END
// LAB-S02.Q08:R13-BEGIN
[[maybe_unused]] auto SortState::do_rescan([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R13: reset materializing sort state",
        }
    );
}

// LAB-S02.Q08:R13-END
auto SortState::do_close(ExecContext& context) -> base::Status {
    entries_.clear();
    next_entry_ = 0U;
    sorted_ = false;
    return input_->close(context);
}

}  // namespace db::execution
