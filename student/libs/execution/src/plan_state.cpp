#include "db/execution/plan_state.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "db/base/error.hpp"

namespace db::execution {
namespace {

auto lifecycle_name(const PlanStateLifecycle lifecycle) noexcept -> std::string_view {
    switch (lifecycle) {
        case PlanStateLifecycle::kCreated:
            return "created";
        case PlanStateLifecycle::kOpen:
            return "open";
        case PlanStateLifecycle::kExhausted:
            return "exhausted";
        case PlanStateLifecycle::kFailed:
            return "failed";
        case PlanStateLifecycle::kClosed:
            return "closed";
    }
    return "unknown";
}

auto lifecycle_error(const std::string_view operation, const PlanStateLifecycle lifecycle)
    -> base::Error {
    return base::Error{
        base::ErrorCode::kExecution,
        "plan state cannot " + std::string{operation} + " while it is "
            + std::string{lifecycle_name(lifecycle)},
    }
        .with_context("execution.plan_state", "lifecycle");
}

auto output_error(const std::string_view message) -> base::Error {
    return base::Error{
        base::ErrorCode::kExecution,
        std::string{message}
    }.with_context("execution.plan_state", "produce tuple");
}

}  // namespace

PlanState::PlanState(TupleSlot output_slot)
    : output_slot_{std::move(output_slot)} {}

void PlanState::recycle_output_values() noexcept {
    if (!output_slot_.occupied_) {
        return;
    }
    recycled_output_values_ = std::move(output_slot_.tuple_.values_);
    output_slot_.tuple_.rid_.reset();
    output_slot_.occupied_ = false;
}

auto PlanState::open(ExecContext& context) -> base::Status {
    if (lifecycle_ != PlanStateLifecycle::kCreated) {
        return base::fail(lifecycle_error("open", lifecycle_));
    }
    output_slot_.clear();
    recycled_output_values_.clear();
    auto opened = do_open(context);
    if (!opened) {
        output_slot_.clear();
        lifecycle_ = PlanStateLifecycle::kFailed;
        return base::fail(std::move(opened).error().with_context("execution.plan_state", "open"));
    }
    if (!output_slot_.empty()) {
        output_slot_.clear();
        lifecycle_ = PlanStateLifecycle::kFailed;
        return base::fail(output_error("plan state stored a tuple while opening"));
    }
    lifecycle_ = PlanStateLifecycle::kOpen;
    return {};
}

auto PlanState::next(ExecContext& context) -> base::Result<bool> {
    if (lifecycle_ == PlanStateLifecycle::kCreated || lifecycle_ == PlanStateLifecycle::kFailed
        || lifecycle_ == PlanStateLifecycle::kClosed) {
        return base::fail(lifecycle_error("produce a tuple", lifecycle_));
    }
    if (lifecycle_ == PlanStateLifecycle::kExhausted) {
        output_slot_.clear();
        return false;
    }

    recycle_output_values();
    auto produced = do_next(context);
    if (!produced) {
        output_slot_.clear();
        lifecycle_ = PlanStateLifecycle::kFailed;
        return base::fail(std::move(produced).error().with_context("execution.plan_state", "next"));
    }
    if (produced.value()) {
        if (output_slot_.empty()) {
            lifecycle_ = PlanStateLifecycle::kFailed;
            return base::fail(output_error("plan state reported a tuple without storing one"));
        }
        return true;
    }
    if (!output_slot_.empty()) {
        output_slot_.clear();
        lifecycle_ = PlanStateLifecycle::kFailed;
        return base::fail(output_error("plan state stored a tuple while reporting end-of-stream"));
    }
    lifecycle_ = PlanStateLifecycle::kExhausted;
    return false;
}

auto PlanState::rescan(ExecContext& context) -> base::Status {
    if (lifecycle_ != PlanStateLifecycle::kOpen && lifecycle_ != PlanStateLifecycle::kExhausted) {
        return base::fail(lifecycle_error("rescan", lifecycle_));
    }
    recycle_output_values();
    auto rescanned = do_rescan(context);
    if (!rescanned) {
        output_slot_.clear();
        lifecycle_ = PlanStateLifecycle::kFailed;
        return base::fail(
            std::move(rescanned).error().with_context("execution.plan_state", "rescan")
        );
    }
    if (!output_slot_.empty()) {
        output_slot_.clear();
        lifecycle_ = PlanStateLifecycle::kFailed;
        return base::fail(output_error("plan state stored a tuple while rescanning"));
    }
    lifecycle_ = PlanStateLifecycle::kOpen;
    return {};
}

auto PlanState::close(ExecContext& context) -> base::Status {
    if (lifecycle_ == PlanStateLifecycle::kClosed) {
        return {};
    }

    output_slot_.clear();
    recycled_output_values_.clear();
    if (lifecycle_ == PlanStateLifecycle::kCreated) {
        lifecycle_ = PlanStateLifecycle::kClosed;
        return {};
    }

    auto closed = do_close(context);
    output_slot_.clear();
    recycled_output_values_.clear();
    lifecycle_ = PlanStateLifecycle::kClosed;
    if (!closed) {
        return base::fail(std::move(closed).error().with_context("execution.plan_state", "close"));
    }
    return {};
}

auto PlanState::lifecycle() const noexcept -> PlanStateLifecycle {
    return lifecycle_;
}

auto PlanState::output_slot() const noexcept -> const TupleSlot& {
    return output_slot_;
}

auto PlanState::take_output_values() noexcept -> std::optional<std::vector<types::Value>> {
    if (!output_slot_.occupied_) {
        return std::nullopt;
    }
    std::vector<types::Value> values = std::move(output_slot_.tuple_.values_);
    output_slot_.tuple_.rid_.reset();
    output_slot_.occupied_ = false;
    return values;
}

void PlanState::store_output(Tuple tuple) {
    output_slot_.tuple_.values_ = std::move(tuple.values_);
    output_slot_.tuple_.rid_ = tuple.rid_;
    output_slot_.occupied_ = true;
}

void PlanState::store_output_values(
    std::vector<types::Value> values,
    const std::optional<base::Rid> rid
) {
    output_slot_.tuple_.values_ = std::move(values);
    output_slot_.tuple_.rid_ = rid;
    output_slot_.occupied_ = true;
}

void PlanState::transfer_output_from(PlanState& child) {
    if (recycled_output_values_.capacity() > child.recycled_output_values_.capacity()) {
        recycled_output_values_.swap(child.recycled_output_values_);
    }
    output_slot_.tuple_.values_ = std::move(child.output_slot_.tuple_.values_);
    output_slot_.tuple_.rid_ = child.output_slot_.tuple_.rid_;
    output_slot_.occupied_ = child.output_slot_.occupied_;
    child.output_slot_.tuple_.rid_.reset();
    child.output_slot_.occupied_ = false;
}

void PlanState::transfer_output_values_from(PlanState& child) {
    transfer_output_from(child);
    if (output_slot_.occupied_) {
        output_slot_.tuple_.rid_.reset();
    }
}

auto PlanState::take_output_from(PlanState& child) -> Tuple {
    Tuple tuple{
        child.output_slot_.descriptor_,
        std::move(child.output_slot_.tuple_.values_),
        child.output_slot_.tuple_.rid_,
    };
    child.output_slot_.tuple_.rid_.reset();
    child.output_slot_.occupied_ = false;
    return tuple;
}

auto PlanState::take_recycled_output_values() noexcept -> std::vector<types::Value> {
    return std::move(recycled_output_values_);
}

}  // namespace db::execution
