#include "db/execution/topn.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "state_support.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto topn_error(const base::ErrorCode code, std::string message) -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.topn", "construct");
}

}  // namespace

TopNState::TopNState(
    TupleSlot output_slot,
    std::unique_ptr<PlanState> input,
    SortSupport support,
    RowCountSupport row_count
)
    : PlanState{std::move(output_slot)},
      input_{std::move(input)},
      support_{std::move(support)},
      row_count_{std::move(row_count)} {}

auto TopNState::create(const planner::TopNPlan& plan, std::unique_ptr<PlanState> input)
    -> base::Result<std::unique_ptr<TopNState>> {
    if (input == nullptr) {
        return base::fail(topn_error(base::ErrorCode::kPlan, "Top-N input is absent"));
    }
    if (plan.limit == nullptr) {
        return base::fail(topn_error(base::ErrorCode::kPlan, "Top-N limit is absent"));
    }
    auto slot = detail::output_slot_for_schema(plan.metadata.output);
    if (!slot) {
        return base::fail(std::move(slot).error());
    }
    auto shape = detail::validate_passthrough_shape(
        input->output_slot().descriptor(),
        slot->descriptor(),
        "Top-N"
    );
    if (!shape) {
        return base::fail(std::move(shape).error());
    }
    auto support = SortSupport::compile(plan.order_by);
    if (!support) {
        return base::fail(std::move(support).error());
    }
    auto row_count = RowCountSupport::compile(plan.limit.get(), plan.offset.get());
    if (!row_count) {
        return base::fail(std::move(row_count).error());
    }
    return std::unique_ptr<TopNState>{new TopNState{
        std::move(slot).value(),
        std::move(input),
        std::move(support).value(),
        std::move(row_count).value(),
    }};
}

// LAB-S04.Q05:R01-BEGIN
[[maybe_unused]] auto TopNState::entry_precedes(
    [[maybe_unused]] const Entry&,
    [[maybe_unused]] const Entry&
) const noexcept -> bool {
    base::lab::missing_algorithm("S04.Q05:R01");
}

[[maybe_unused]] auto TopNState::entry_precedes(
    [[maybe_unused]] const std::span<const types::Value>,
    [[maybe_unused]] const std::size_t,
    [[maybe_unused]] const Entry&
) const noexcept -> bool {
    base::lab::missing_algorithm("S04.Q05:R01");
}

// LAB-S04.Q05:R01-END
// LAB-S04.Q05:R02-BEGIN
[[maybe_unused]] auto TopNState::resolve_window([[maybe_unused]] const ExecContext&)
    -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S04.Q05:R02: resolve the bounded Top-N window",
        }
    );
}

// LAB-S04.Q05:R02-END
// LAB-S04.Q05:R03-BEGIN
[[maybe_unused]] auto TopNState::do_open([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S04.Q05:R03: initialize bounded Top-N state",
        }
    );
}

// LAB-S04.Q05:R03-END
// LAB-S04.Q05:R04-BEGIN
[[maybe_unused]] void TopNState::replace_worst([[maybe_unused]] Entry) {
    base::lab::missing_algorithm("S04.Q05:R04");
}

// LAB-S04.Q05:R04-END
// LAB-S04.Q05:R05-BEGIN
[[maybe_unused]] auto TopNState::fill([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S04.Q05:R05: maintain the bounded Top-N candidate heap",
        }
    );
}

// LAB-S04.Q05:R05-END
// LAB-S04.Q05:R06-BEGIN
[[maybe_unused]] auto TopNState::do_next([[maybe_unused]] ExecContext&) -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S04.Q05:R06: emit the bounded Top-N window",
        }
    );
}

// LAB-S04.Q05:R06-END
// LAB-S04.Q05:R07-BEGIN
[[maybe_unused]] auto TopNState::do_rescan([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S04.Q05:R07: reset bounded Top-N state",
        }
    );
}

// LAB-S04.Q05:R07-END
auto TopNState::do_close(ExecContext& context) -> base::Status {
    heap_.clear();
    limit_ = 0U;
    offset_ = 0U;
    next_entry_ = 0U;
    capacity_ = 0U;
    filled_ = false;
    return input_->close(context);
}

}  // namespace db::execution
