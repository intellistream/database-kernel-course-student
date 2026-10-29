#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/execution/append.hpp"

#include "fixed_rows_state.hpp"

namespace db::execution {
namespace {

using test::RowsCall;
using test::RowsFault;
using test::RowsTrace;

auto empty_context() -> base::Result<ExecContext> {
    auto parameters = planner::ParameterNamespace::from_external_slots({});
    if (!parameters) {
        return base::fail(std::move(parameters).error());
    }
    return ExecContext::create(*parameters, {});
}

auto schema() -> planner::PlanSchema {
    return {.columns = {{.name = "id", .data_type = types::kInt64Type}}};
}

auto child(
    const std::vector<std::int64_t>& values,
    const std::shared_ptr<RowsTrace>& trace,
    std::vector<RowsFault> faults = {}
) -> std::unique_ptr<PlanState> {
    std::vector<std::vector<types::Value>> rows;
    for (const auto value : values) {
        rows.push_back({types::Value::integer(value)});
    }
    auto state = test::FixedRowsState::create(schema(), std::move(rows), trace, std::move(faults));
    EXPECT_TRUE(state);
    return state ? std::move(*state) : nullptr;
}

auto append(std::vector<std::unique_ptr<PlanState>> children)
    -> base::Result<std::unique_ptr<AppendState>> {
    planner::AppendPlan plan{.metadata = {.output = schema()}, .inputs = {}};
    // The factory receives independently supplied child ports; only their
    // count and output shapes are needed from this input-plan DTO.
    plan.inputs.resize(children.size());
    return AppendState::create(plan, std::move(children));
}

auto drain(PlanState& state, ExecContext& context) -> base::Result<std::vector<std::int64_t>> {
    std::vector<std::int64_t> values;
    while (true) {
        auto next = state.next(context);
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!*next) {
            return values;
        }
        values.push_back(*state.output_slot().tuple()->values()[0U].as_integer());
    }
}

TEST(LabS06Q02Test, ConcatenatesStablyAndOpensChildrenOnlyWhenNeeded) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto a = std::make_shared<RowsTrace>();
    auto b = std::make_shared<RowsTrace>();
    auto c = std::make_shared<RowsTrace>();
    std::vector<std::unique_ptr<PlanState>> children;
    children.push_back(child({}, a));
    children.push_back(child({3, 1, 1}, b));
    children.push_back(child({4, 0}, c));
    auto state = append(std::move(children));
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    EXPECT_TRUE(a->events.empty());
    EXPECT_TRUE(b->events.empty());
    EXPECT_TRUE(c->events.empty());
    auto first = (*state)->next(*context);
    ASSERT_TRUE(first && *first);
    EXPECT_EQ((*state)->output_slot().tuple()->values()[0U], types::Value::integer(3));
    EXPECT_EQ(a->count(RowsCall::kNext), 1U);
    EXPECT_EQ(b->count(RowsCall::kNext), 1U);
    EXPECT_TRUE(c->events.empty());
    auto rest = drain(**state, *context);
    ASSERT_TRUE(rest);
    EXPECT_EQ(*rest, (std::vector<std::int64_t>{1, 1, 4, 0}));
    EXPECT_EQ(b->count(RowsCall::kNext), 4U);
    EXPECT_EQ(c->count(RowsCall::kNext), 3U);
    ASSERT_TRUE((*state)->close(*context));
    for (const auto& trace : {a, b, c}) {
        EXPECT_EQ(trace->count(RowsCall::kOpen), 1U);
        EXPECT_EQ(trace->count(RowsCall::kClose), 1U);
        for (const auto& event : trace->events) {
            EXPECT_EQ(event.context, &*context);
        }
    }
}

TEST(LabS06Q02Test, EmptyChildrenHaveStableEndAndCanBeRescanned) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto a = std::make_shared<RowsTrace>();
    auto b = std::make_shared<RowsTrace>();
    std::vector<std::unique_ptr<PlanState>> children;
    children.push_back(child({}, a));
    children.push_back(child({}, b));
    auto state = append(std::move(children));
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    auto rows = drain(**state, *context);
    ASSERT_TRUE(rows);
    EXPECT_TRUE(rows->empty());
    auto repeated = (*state)->next(*context);
    ASSERT_TRUE(repeated);
    EXPECT_FALSE(*repeated);
    EXPECT_EQ(a->count(RowsCall::kNext), 1U);
    EXPECT_EQ(b->count(RowsCall::kNext), 1U);
    ASSERT_TRUE((*state)->rescan(*context));
    rows = drain(**state, *context);
    ASSERT_TRUE(rows);
    EXPECT_TRUE(rows->empty());
    EXPECT_EQ(a->count(RowsCall::kRescan), 1U);
    EXPECT_EQ(b->count(RowsCall::kRescan), 1U);
    ASSERT_TRUE((*state)->close(*context));
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(a->count(RowsCall::kClose), 1U);
    EXPECT_EQ(b->count(RowsCall::kClose), 1U);
}

TEST(LabS06Q02Test, RescanRewindsVisitedChildrenAndLeavesUnvisitedOnesClosed) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto a = std::make_shared<RowsTrace>();
    auto b = std::make_shared<RowsTrace>();
    std::vector<std::unique_ptr<PlanState>> children;
    children.push_back(child({2, 2}, a));
    children.push_back(child({9}, b));
    auto state = append(std::move(children));
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    auto first = (*state)->next(*context);
    ASSERT_TRUE(first && *first);
    ASSERT_TRUE((*state)->rescan(*context));
    EXPECT_TRUE((*state)->output_slot().empty());
    EXPECT_EQ(a->count(RowsCall::kRescan), 1U);
    EXPECT_TRUE(b->events.empty());
    auto rows = drain(**state, *context);
    ASSERT_TRUE(rows);
    EXPECT_EQ(*rows, (std::vector<std::int64_t>{2, 2, 9}));
    ASSERT_TRUE((*state)->rescan(*context));
    rows = drain(**state, *context);
    ASSERT_TRUE(rows);
    EXPECT_EQ(*rows, (std::vector<std::int64_t>{2, 2, 9}));
    EXPECT_EQ(a->count(RowsCall::kRescan), 2U);
    EXPECT_EQ(b->count(RowsCall::kRescan), 1U);
    ASSERT_TRUE((*state)->close(*context));
}

TEST(LabS06Q02Test, NextFailureDoesNotOpenLaterChildrenOrLeaveStaleOutput) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto a = std::make_shared<RowsTrace>();
    auto b = std::make_shared<RowsTrace>();
    std::vector<std::unique_ptr<PlanState>> children;
    children.push_back(
        child({7, 8}, a, {{RowsCall::kNext, 2U, {base::ErrorCode::kIo, "scripted next"}}})
    );
    children.push_back(child({9}, b));
    auto state = append(std::move(children));
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    auto first = (*state)->next(*context);
    ASSERT_TRUE(first && *first);
    auto failed = (*state)->next(*context);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(failed.error().message(), "scripted next");
    EXPECT_TRUE((*state)->output_slot().empty());
    EXPECT_EQ((*state)->lifecycle(), PlanStateLifecycle::kFailed);
    EXPECT_TRUE(b->events.empty());
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(a->count(RowsCall::kClose), 1U);
    EXPECT_EQ(b->count(RowsCall::kClose), 0U);
}

TEST(LabS06Q02Test, FailedChildOpenCleansVisitedInputsAndKeepsThePrimaryError) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto a = std::make_shared<RowsTrace>();
    auto b = std::make_shared<RowsTrace>();
    auto c = std::make_shared<RowsTrace>();
    std::vector<std::unique_ptr<PlanState>> children;
    children.push_back(
        child({}, a, {{RowsCall::kClose, 1U, {base::ErrorCode::kIo, "scripted close"}}})
    );
    children.push_back(
        child({2}, b, {{RowsCall::kOpen, 1U, {base::ErrorCode::kUnavailable, "scripted open"}}})
    );
    children.push_back(child({3}, c));
    auto state = append(std::move(children));
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    auto failed = (*state)->next(*context);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(failed.error().message(), "scripted open");
    EXPECT_NE(failed.error().to_string().find("scripted close"), std::string::npos);
    EXPECT_EQ(a->count(RowsCall::kClose), 1U);
    EXPECT_EQ(b->count(RowsCall::kClose), 1U);
    EXPECT_TRUE(c->events.empty());
    ASSERT_TRUE((*state)->close(*context));
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(a->count(RowsCall::kClose), 1U);
    EXPECT_EQ(b->count(RowsCall::kClose), 1U);
}

TEST(LabS06Q02Test, RescanFailureStopsRestartAndRemainsSafeToClose) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto a = std::make_shared<RowsTrace>();
    auto b = std::make_shared<RowsTrace>();
    std::vector<std::unique_ptr<PlanState>> children;
    children.push_back(
        child({1}, a, {{RowsCall::kRescan, 1U, {base::ErrorCode::kIo, "scripted rescan"}}})
    );
    children.push_back(child({2}, b));
    auto state = append(std::move(children));
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    ASSERT_TRUE(drain(**state, *context));
    auto failed = (*state)->rescan(*context);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().message(), "scripted rescan");
    EXPECT_EQ(b->count(RowsCall::kRescan), 0U);
    EXPECT_TRUE((*state)->output_slot().empty());
    EXPECT_FALSE((*state)->next(*context));
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(a->count(RowsCall::kClose), 1U);
    EXPECT_EQ(b->count(RowsCall::kClose), 1U);
}

}  // namespace
}  // namespace db::execution
