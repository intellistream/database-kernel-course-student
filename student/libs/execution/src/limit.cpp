#include "db/execution/limit.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "db/base/error.hpp"

#include "state_support.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto limit_error(const base::ErrorCode code, std::string message) -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.limit", "construct");
}

}  // namespace

LimitState::LimitState(
    TupleSlot output_slot,
    std::unique_ptr<PlanState> input,
    RowCountSupport row_count
)
    : PlanState{std::move(output_slot)},
      input_{std::move(input)},
      row_count_{std::move(row_count)} {}

auto LimitState::create(const planner::LimitPlan& plan, std::unique_ptr<PlanState> input)
    -> base::Result<std::unique_ptr<LimitState>> {
    if (input == nullptr) {
        return base::fail(limit_error(base::ErrorCode::kPlan, "limit input is absent"));
    }
    if (plan.limit == nullptr && plan.offset == nullptr) {
        return base::fail(
            limit_error(base::ErrorCode::kPlan, "limit node has neither limit nor offset")
        );
    }
    auto slot = detail::output_slot_for_schema(plan.metadata.output);
    if (!slot) {
        return base::fail(std::move(slot).error());
    }
    auto shape = detail::validate_passthrough_shape(
        input->output_slot().descriptor(),
        slot->descriptor(),
        "limit"
    );
    if (!shape) {
        return base::fail(std::move(shape).error());
    }
    auto row_count = RowCountSupport::compile(plan.limit.get(), plan.offset.get());
    if (!row_count) {
        return base::fail(std::move(row_count).error());
    }
    return std::unique_ptr<LimitState>{new LimitState{
        std::move(slot).value(),
        std::move(input),
        std::move(row_count).value(),
    }};
}

// LAB-S02.Q08:R07-BEGIN
[[maybe_unused]] auto LimitState::do_open([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R07: initialize the LIMIT/OFFSET window",
        }
    );
}

// LAB-S02.Q08:R07-END
// LAB-S02.Q08:R08-BEGIN
[[maybe_unused]] auto LimitState::do_next([[maybe_unused]] ExecContext&) -> base::Result<bool> {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R08: stream the LIMIT/OFFSET window",
        }
    );
}

// LAB-S02.Q08:R08-END
// LAB-S02.Q08:R09-BEGIN
[[maybe_unused]] auto LimitState::do_rescan([[maybe_unused]] ExecContext&) -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S02.Q08:R09: reset the LIMIT/OFFSET window",
        }
    );
}

// LAB-S02.Q08:R09-END

auto LimitState::do_close(ExecContext& context) -> base::Status {
    limit_.reset();
    offset_ = 0U;
    skipped_ = 0U;
    emitted_ = 0U;
    return input_->close(context);
}

}  // namespace db::execution
