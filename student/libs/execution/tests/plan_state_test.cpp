#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/execution/append.hpp"
#include "db/execution/plan_state.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::execution {
namespace {

auto empty_context() -> base::Result<ExecContext> {
    const std::vector<planner::ExternalParameterSlot> external_slots;
    auto parameter_namespace = planner::ParameterNamespace::from_external_slots(external_slots);
    if (!parameter_namespace) {
        return base::fail(std::move(parameter_namespace).error());
    }
    return ExecContext::create(parameter_namespace.value(), {});
}

auto make_descriptor() -> TupleDescriptorHandle {
    auto descriptor = TupleDescriptor::create({
        TupleColumn{.name = "id", .data_type = types::kInt64Type},
    });
    EXPECT_TRUE(descriptor) << descriptor.error().to_string();
    return std::move(descriptor).value();
}

enum class NextFault : std::uint8_t {
    kNone,
    kError,
    kTupleWithoutStore,
    kEndWithStoredTuple,
};

class CountingPlanState final : public PlanState {
public:
    CountingPlanState(TupleSlot output_slot, std::vector<std::int64_t> values)
        : PlanState{std::move(output_slot)},
          values_{std::move(values)} {}

    void set_open_failure(const bool enabled) noexcept {
        open_failure_ = enabled;
    }

    void set_rescan_failure(const bool enabled) noexcept {
        rescan_failure_ = enabled;
    }

    void set_open_stores_tuple(const bool enabled) noexcept {
        open_stores_tuple_ = enabled;
    }

    void set_rescan_stores_tuple(const bool enabled) noexcept {
        rescan_stores_tuple_ = enabled;
    }

    void set_close_failure(const bool enabled) noexcept {
        close_failure_ = enabled;
    }

    void set_next_fault(const NextFault fault) noexcept {
        next_fault_ = fault;
    }

    auto open_calls() const noexcept -> std::size_t {
        return open_calls_;
    }

    auto next_calls() const noexcept -> std::size_t {
        return next_calls_;
    }

    auto rescan_calls() const noexcept -> std::size_t {
        return rescan_calls_;
    }

    auto close_calls() const noexcept -> std::size_t {
        return close_calls_;
    }

    auto last_output_storage() const noexcept -> const types::Value* {
        return last_output_storage_;
    }

protected:
    auto do_open(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        ++open_calls_;
        if (open_failure_) {
            return base::fail(base::Error{base::ErrorCode::kExecution, "open failure"});
        }
        if (open_stores_tuple_) {
            store_output_values({types::Value::integer(998)});
        }
        return {};
    }

    auto do_next(ExecContext& context) -> base::Result<bool> override {
        static_cast<void>(context);
        ++next_calls_;
        switch (next_fault_) {
            case NextFault::kError:
                return base::fail(base::Error{base::ErrorCode::kExecution, "next failure"});
            case NextFault::kTupleWithoutStore:
                return true;
            case NextFault::kEndWithStoredTuple: {
                store_output_values({types::Value::integer(999)});
                return false;
            }
            case NextFault::kNone:
                break;
        }
        if (cursor_ == values_.size()) {
            return false;
        }
        std::vector<types::Value> output = take_recycled_output_values();
        output.clear();
        output.push_back(types::Value::integer(values_[cursor_]));
        last_output_storage_ = output.data();
        store_output_values(std::move(output));
        ++cursor_;
        return true;
    }

    auto do_rescan(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        ++rescan_calls_;
        if (rescan_failure_) {
            if (rescan_stores_tuple_) {
                store_output_values({types::Value::integer(997)});
            }
            return base::fail(base::Error{base::ErrorCode::kExecution, "rescan failure"});
        }
        if (rescan_stores_tuple_) {
            store_output_values({types::Value::integer(997)});
        }
        cursor_ = 0U;
        return {};
    }

    auto do_close(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        ++close_calls_;
        if (close_failure_) {
            return base::fail(base::Error{base::ErrorCode::kExecution, "close failure"});
        }
        return {};
    }

private:
    std::vector<std::int64_t> values_;
    std::size_t cursor_{0U};
    std::size_t open_calls_{0U};
    std::size_t next_calls_{0U};
    std::size_t rescan_calls_{0U};
    std::size_t close_calls_{0U};
    const types::Value* last_output_storage_{nullptr};
    bool open_failure_{false};
    bool rescan_failure_{false};
    bool close_failure_{false};
    bool open_stores_tuple_{false};
    bool rescan_stores_tuple_{false};
    NextFault next_fault_{NextFault::kNone};
};

class PassthroughPlanState final : public PlanState {
public:
    PassthroughPlanState(TupleSlot output_slot, std::unique_ptr<PlanState> input)
        : PlanState{std::move(output_slot)},
          input_{std::move(input)} {}

protected:
    auto do_open(ExecContext& context) -> base::Status override {
        return input_->open(context);
    }

    auto do_next(ExecContext& context) -> base::Result<bool> override {
        auto produced = input_->next(context);
        if (!produced) {
            return base::fail(std::move(produced).error());
        }
        if (!produced.value()) {
            return false;
        }
        transfer_output_from(*input_);
        return true;
    }

    auto do_rescan(ExecContext& context) -> base::Status override {
        return input_->rescan(context);
    }

    auto do_close(ExecContext& context) -> base::Status override {
        return input_->close(context);
    }

private:
    std::unique_ptr<PlanState> input_;
};

auto make_state(std::vector<std::int64_t> values) -> CountingPlanState {
    auto slot = TupleSlot::create(make_descriptor());
    EXPECT_TRUE(slot) << slot.error().to_string();
    return CountingPlanState{std::move(slot).value(), std::move(values)};
}

auto append_child(std::vector<std::int64_t> values) -> std::unique_ptr<CountingPlanState> {
    auto slot = TupleSlot::create(make_descriptor());
    EXPECT_TRUE(slot);
    return std::make_unique<CountingPlanState>(std::move(*slot), std::move(values));
}

auto append_plan(const std::size_t count) -> planner::AppendPlan {
    planner::AppendPlan plan{
        .metadata =
            planner::PlanOperatorMetadata{
                .output =
                    planner::PlanSchema{
                        .columns =
                            {
                                planner::PlanOutputColumn{
                                    .name = "id",
                                    .data_type = types::kInt64Type
                                },
                            }
                    }
            },
        .inputs = {},
    };
    plan.inputs.resize(count);
    return plan;
}

TEST(AppendStateTest, StreamsLazilyAndRescansPartiallyVisitedChildren) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto first = append_child({1, 1});
    auto second = append_child({2});
    auto* first_view = first.get();
    auto* second_view = second.get();
    std::vector<std::unique_ptr<PlanState>> children;
    children.push_back(std::move(first));
    children.push_back(std::move(second));
    auto state = AppendState::create(append_plan(2U), std::move(children));
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    EXPECT_EQ(first_view->open_calls(), 0U);
    EXPECT_EQ(second_view->open_calls(), 0U);
    auto next = (*state)->next(*context);
    ASSERT_TRUE(next && *next);
    EXPECT_EQ(first_view->open_calls(), 1U);
    EXPECT_EQ(second_view->open_calls(), 0U);
    ASSERT_TRUE((*state)->rescan(*context));
    EXPECT_EQ(first_view->rescan_calls(), 1U);
    EXPECT_EQ(second_view->rescan_calls(), 0U);
    for (const auto expected : {1, 1, 2}) {
        next = (*state)->next(*context);
        ASSERT_TRUE(next && *next);
        EXPECT_EQ((*state)->output_slot().tuple()->values()[0], types::Value::integer(expected));
    }
    next = (*state)->next(*context);
    ASSERT_TRUE(next);
    EXPECT_FALSE(*next);
    ASSERT_TRUE((*state)->rescan(*context));
    EXPECT_EQ(second_view->rescan_calls(), 1U);
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(first_view->close_calls(), 1U);
    EXPECT_EQ(second_view->close_calls(), 1U);
    EXPECT_TRUE((*state)->close(*context));
    EXPECT_EQ(first_view->close_calls(), 1U);
}

TEST(AppendStateTest, PartialOpenFailureCleansAllVisitedChildrenAndPreservesPrimaryError) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto first = append_child({});
    auto second = append_child({2});
    auto third = append_child({3});
    first->set_close_failure(true);
    second->set_open_failure(true);
    auto* a = first.get();
    auto* b = second.get();
    auto* c = third.get();
    std::vector<std::unique_ptr<PlanState>> children;
    children.push_back(std::move(first));
    children.push_back(std::move(second));
    children.push_back(std::move(third));
    auto state = AppendState::create(append_plan(3U), std::move(children));
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    auto next = (*state)->next(*context);
    ASSERT_FALSE(next);
    EXPECT_EQ(next.error().message(), "open failure");
    EXPECT_EQ(a->close_calls(), 1U);
    EXPECT_EQ(b->close_calls(), 1U);
    EXPECT_EQ(c->open_calls(), 0U);
    EXPECT_EQ(c->close_calls(), 0U);
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(a->close_calls(), 1U);
    EXPECT_EQ(b->close_calls(), 1U);
}

TEST(AppendStateTest, RejectsMissingOrMismatchedChildren) {
    auto missing = AppendState::create(append_plan(0U), {});
    ASSERT_FALSE(missing);
    std::vector<std::unique_ptr<PlanState>> absent;
    absent.push_back(nullptr);
    EXPECT_FALSE(AppendState::create(append_plan(1U), std::move(absent)));
    std::vector<std::unique_ptr<PlanState>> extra;
    extra.push_back(append_child({1}));
    EXPECT_FALSE(AppendState::create(append_plan(2U), std::move(extra)));
}

TEST(PlanStateTest, EnforcesLifecycleAndMakesEndOfStreamStable) {
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    auto state = make_state({3, 5});

    const auto before_open = state.next(context.value());
    ASSERT_FALSE(before_open);
    EXPECT_EQ(before_open.error().code(), base::ErrorCode::kExecution);
    EXPECT_EQ(state.lifecycle(), PlanStateLifecycle::kCreated);
    const auto rescan_before_open = state.rescan(context.value());
    ASSERT_FALSE(rescan_before_open);
    EXPECT_EQ(rescan_before_open.error().code(), base::ErrorCode::kExecution);

    ASSERT_TRUE(state.open(context.value()));
    EXPECT_EQ(state.lifecycle(), PlanStateLifecycle::kOpen);
    EXPECT_EQ(state.open_calls(), 1U);
    const auto second_open = state.open(context.value());
    ASSERT_FALSE(second_open);
    EXPECT_EQ(second_open.error().code(), base::ErrorCode::kExecution);

    const auto first = state.next(context.value());
    ASSERT_TRUE(first) << first.error().to_string();
    ASSERT_TRUE(first.value());
    ASSERT_NE(state.output_slot().tuple(), nullptr);
    EXPECT_EQ(state.output_slot().tuple()->values()[0U], types::Value::integer(3));

    const auto second = state.next(context.value());
    ASSERT_TRUE(second) << second.error().to_string();
    ASSERT_TRUE(second.value());
    ASSERT_NE(state.output_slot().tuple(), nullptr);
    EXPECT_EQ(state.output_slot().tuple()->values()[0U], types::Value::integer(5));

    const auto end = state.next(context.value());
    ASSERT_TRUE(end) << end.error().to_string();
    EXPECT_FALSE(end.value());
    EXPECT_EQ(state.lifecycle(), PlanStateLifecycle::kExhausted);
    EXPECT_TRUE(state.output_slot().empty());
    EXPECT_EQ(state.next_calls(), 3U);

    const auto repeated_end = state.next(context.value());
    ASSERT_TRUE(repeated_end) << repeated_end.error().to_string();
    EXPECT_FALSE(repeated_end.value());
    EXPECT_EQ(state.next_calls(), 3U);
}

TEST(PlanStateTest, TransfersOutputValuesToATerminalConsumerWithoutCopying) {
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    auto state = make_state({3, 5});
    ASSERT_TRUE(state.open(context.value()));
    EXPECT_FALSE(state.take_output_values().has_value());

    auto next = state.next(context.value());
    ASSERT_TRUE(next) << next.error().to_string();
    ASSERT_TRUE(next.value());
    const types::Value* const first_storage = state.output_slot().tuple()->values().data();
    auto first = state.take_output_values();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->data(), first_storage);
    ASSERT_EQ(first->size(), 1U);
    EXPECT_EQ(first->front(), types::Value::integer(3));
    EXPECT_TRUE(state.output_slot().empty());
    EXPECT_EQ(state.lifecycle(), PlanStateLifecycle::kOpen);

    next = state.next(context.value());
    ASSERT_TRUE(next) << next.error().to_string();
    ASSERT_TRUE(next.value());
    auto second = state.take_output_values();
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(second->size(), 1U);
    EXPECT_EQ(second->front(), types::Value::integer(5));

    next = state.next(context.value());
    ASSERT_TRUE(next) << next.error().to_string();
    EXPECT_FALSE(next.value());
    EXPECT_EQ(state.lifecycle(), PlanStateLifecycle::kExhausted);
}

TEST(PlanStateTest, RecyclesAnUnconsumedOutputAllocationOnTheNextPull) {
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    auto state = make_state({3, 5});
    ASSERT_TRUE(state.open(context.value()));
    ASSERT_TRUE(state.next(context.value()));
    const types::Value* const first_storage = state.last_output_storage();
    ASSERT_NE(first_storage, nullptr);

    ASSERT_TRUE(state.next(context.value()));
    EXPECT_EQ(state.last_output_storage(), first_storage);
    ASSERT_NE(state.output_slot().tuple(), nullptr);
    EXPECT_EQ(state.output_slot().tuple()->values()[0U], types::Value::integer(5));
}

TEST(PlanStateTest, ReturnsReclaimedStorageThroughAPassthroughState) {
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    auto source_slot = TupleSlot::create(make_descriptor());
    ASSERT_TRUE(source_slot) << source_slot.error().to_string();
    auto inner_slot = TupleSlot::create(make_descriptor());
    ASSERT_TRUE(inner_slot) << inner_slot.error().to_string();
    auto parent_slot = TupleSlot::create(make_descriptor());
    ASSERT_TRUE(parent_slot) << parent_slot.error().to_string();
    auto source = std::make_unique<CountingPlanState>(
        std::move(source_slot).value(),
        std::vector<std::int64_t>{3, 5, 7, 11}
    );
    auto inner =
        std::make_unique<PassthroughPlanState>(std::move(inner_slot).value(), std::move(source));
    PassthroughPlanState state{
        std::move(parent_slot).value(),
        std::move(inner),
    };
    ASSERT_TRUE(state.open(context.value()));

    ASSERT_TRUE(state.next(context.value()));
    const types::Value* const first_storage = state.output_slot().tuple()->values().data();
    ASSERT_TRUE(state.next(context.value()));
    EXPECT_NE(state.output_slot().tuple()->values().data(), first_storage);
    ASSERT_TRUE(state.next(context.value()));
    EXPECT_NE(state.output_slot().tuple()->values().data(), first_storage);
    ASSERT_TRUE(state.next(context.value()));
    EXPECT_EQ(state.output_slot().tuple()->values().data(), first_storage);
    EXPECT_EQ(state.output_slot().tuple()->values()[0U], types::Value::integer(11));

    auto end = state.next(context.value());
    ASSERT_TRUE(end) << end.error().to_string();
    EXPECT_FALSE(end.value());
    EXPECT_TRUE(state.output_slot().empty());

    ASSERT_TRUE(state.rescan(context.value()));
    ASSERT_TRUE(state.next(context.value()));
    EXPECT_EQ(state.output_slot().tuple()->values()[0U], types::Value::integer(3));
}

TEST(PlanStateTest, RescanResetsACompletedStateAndClearsOutput) {
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    auto state = make_state({17});
    ASSERT_TRUE(state.open(context.value()));
    ASSERT_TRUE(state.next(context.value()));
    ASSERT_TRUE(state.next(context.value()));
    EXPECT_EQ(state.lifecycle(), PlanStateLifecycle::kExhausted);

    ASSERT_TRUE(state.rescan(context.value()));
    EXPECT_EQ(state.lifecycle(), PlanStateLifecycle::kOpen);
    EXPECT_TRUE(state.output_slot().empty());
    EXPECT_EQ(state.rescan_calls(), 1U);
    const auto row = state.next(context.value());
    ASSERT_TRUE(row) << row.error().to_string();
    EXPECT_TRUE(row.value());
    ASSERT_NE(state.output_slot().tuple(), nullptr);
    EXPECT_EQ(state.output_slot().tuple()->values()[0U], types::Value::integer(17));
}

TEST(PlanStateTest, MakesOpenFailureCloseOnlyAndMakesCloseTerminal) {
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    auto state = make_state({});
    state.set_open_failure(true);
    const auto failed_open = state.open(context.value());
    ASSERT_FALSE(failed_open);
    EXPECT_EQ(state.lifecycle(), PlanStateLifecycle::kFailed);
    EXPECT_TRUE(state.output_slot().empty());
    EXPECT_EQ(state.close_calls(), 0U);

    state.set_open_failure(false);
    const auto retry = state.open(context.value());
    ASSERT_FALSE(retry);
    EXPECT_EQ(retry.error().code(), base::ErrorCode::kExecution);
    EXPECT_EQ(state.open_calls(), 1U);

    state.set_close_failure(true);
    const auto failed_close = state.close(context.value());
    ASSERT_FALSE(failed_close);
    EXPECT_EQ(state.lifecycle(), PlanStateLifecycle::kClosed);
    EXPECT_EQ(state.close_calls(), 1U);
    EXPECT_TRUE(state.close(context.value()));
    EXPECT_EQ(state.close_calls(), 1U);
    const auto after_close = state.rescan(context.value());
    ASSERT_FALSE(after_close);
    EXPECT_EQ(after_close.error().code(), base::ErrorCode::kExecution);
    const auto next_after_close = state.next(context.value());
    ASSERT_FALSE(next_after_close);
    EXPECT_EQ(next_after_close.error().code(), base::ErrorCode::kExecution);
    const auto open_after_close = state.open(context.value());
    ASSERT_FALSE(open_after_close);
    EXPECT_EQ(open_after_close.error().code(), base::ErrorCode::kExecution);

    auto never_opened = make_state({});
    ASSERT_TRUE(never_opened.close(context.value()));
    EXPECT_EQ(never_opened.lifecycle(), PlanStateLifecycle::kClosed);
    EXPECT_EQ(never_opened.close_calls(), 0U);
    ASSERT_TRUE(never_opened.close(context.value()));
    EXPECT_EQ(never_opened.close_calls(), 0U);
}

TEST(PlanStateTest, MakesNextFailuresCloseOnlyAndClearsStaleSlots) {
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    auto state = make_state({1});
    ASSERT_TRUE(state.open(context.value()));

    state.set_next_fault(NextFault::kTupleWithoutStore);
    const auto missing_tuple = state.next(context.value());
    ASSERT_FALSE(missing_tuple);
    EXPECT_TRUE(state.output_slot().empty());
    EXPECT_EQ(state.lifecycle(), PlanStateLifecycle::kFailed);
    const auto after_failure = state.next(context.value());
    ASSERT_FALSE(after_failure);
    EXPECT_EQ(after_failure.error().code(), base::ErrorCode::kExecution);
    ASSERT_TRUE(state.close(context.value()));
    EXPECT_EQ(state.close_calls(), 1U);
}

TEST(PlanStateTest, MakesOutputContractAndRescanFailuresCloseOnly) {
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();

    auto end_with_tuple = make_state({1});
    ASSERT_TRUE(end_with_tuple.open(context.value()));
    end_with_tuple.set_next_fault(NextFault::kEndWithStoredTuple);
    const auto unexpected_tuple = end_with_tuple.next(context.value());
    ASSERT_FALSE(unexpected_tuple);
    EXPECT_TRUE(end_with_tuple.output_slot().empty());
    EXPECT_EQ(end_with_tuple.lifecycle(), PlanStateLifecycle::kFailed);
    ASSERT_TRUE(end_with_tuple.close(context.value()));

    auto failed_next_state = make_state({1});
    ASSERT_TRUE(failed_next_state.open(context.value()));
    failed_next_state.set_next_fault(NextFault::kError);
    const auto failed_next = failed_next_state.next(context.value());
    ASSERT_FALSE(failed_next);
    EXPECT_TRUE(failed_next_state.output_slot().empty());
    EXPECT_EQ(failed_next_state.lifecycle(), PlanStateLifecycle::kFailed);
    ASSERT_TRUE(failed_next_state.close(context.value()));

    auto failed_rescan_state = make_state({1});
    ASSERT_TRUE(failed_rescan_state.open(context.value()));
    failed_rescan_state.set_rescan_failure(true);
    failed_rescan_state.set_rescan_stores_tuple(true);
    const auto failed_rescan = failed_rescan_state.rescan(context.value());
    ASSERT_FALSE(failed_rescan);
    EXPECT_TRUE(failed_rescan_state.output_slot().empty());
    EXPECT_EQ(failed_rescan_state.lifecycle(), PlanStateLifecycle::kFailed);
    const auto failed_rescan_next = failed_rescan_state.next(context.value());
    ASSERT_FALSE(failed_rescan_next);
    ASSERT_TRUE(failed_rescan_state.close(context.value()));
}

TEST(PlanStateTest, RejectsTuplesStoredDuringOpenOrSuccessfulRescan) {
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();

    auto open_state = make_state({1});
    open_state.set_open_stores_tuple(true);
    const auto invalid_open = open_state.open(context.value());
    ASSERT_FALSE(invalid_open);
    EXPECT_EQ(open_state.lifecycle(), PlanStateLifecycle::kFailed);
    EXPECT_TRUE(open_state.output_slot().empty());
    ASSERT_TRUE(open_state.close(context.value()));

    auto rescan_state = make_state({1});
    ASSERT_TRUE(rescan_state.open(context.value()));
    rescan_state.set_rescan_stores_tuple(true);
    const auto invalid_rescan = rescan_state.rescan(context.value());
    ASSERT_FALSE(invalid_rescan);
    EXPECT_EQ(rescan_state.lifecycle(), PlanStateLifecycle::kFailed);
    EXPECT_TRUE(rescan_state.output_slot().empty());
    ASSERT_TRUE(rescan_state.close(context.value()));
}

}  // namespace
}  // namespace db::execution
