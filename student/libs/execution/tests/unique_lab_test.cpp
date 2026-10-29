#include <bit>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/execution/unique.hpp"

#include "fixed_rows_state.hpp"

namespace db::execution {
namespace {

using Row = std::vector<types::Value>;
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

auto int_schema() -> planner::PlanSchema {
    return {.columns = {{.name = "id", .data_type = types::kInt64Type}}};
}

auto unique(
    const planner::PlanSchema& schema,
    std::vector<Row> rows,
    const std::shared_ptr<RowsTrace>& trace,
    std::vector<RowsFault> faults = {}
) -> base::Result<std::unique_ptr<UniqueState>> {
    auto input = test::FixedRowsState::create(schema, std::move(rows), trace, std::move(faults));
    if (!input) {
        return base::fail(std::move(input).error());
    }
    const planner::UniquePlan plan{.metadata = {.output = schema}, .input = nullptr};
    return UniqueState::create(plan, std::move(*input));
}

auto drain(PlanState& state, ExecContext& context) -> base::Result<std::vector<Row>> {
    std::vector<Row> rows;
    while (true) {
        auto next = state.next(context);
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!*next) {
            return rows;
        }
        const auto values = state.output_slot().tuple()->values();
        rows.emplace_back(values.begin(), values.end());
    }
}

TEST(LabS06Q03Test, ComparesCompleteRowsIncludingTypedNullsAndKeepsFirstOccurrence) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    const planner::PlanSchema schema{
        .columns = {
            {.name = "id", .data_type = types::kInt64Type},
            {.name = "text", .data_type = types::kCharType},
        }
    };
    const auto null_int = types::Value::null(types::kInt64Type);
    const auto null_char = types::Value::null(types::kCharType);
    const Row a{null_int, types::Value::character("x")};
    const Row b{types::Value::integer(1), types::Value::character("x")};
    const Row c{types::Value::integer(1), types::Value::character("y")};
    const Row d{null_int, null_char};
    const Row e{types::Value::integer(2), null_char};
    auto trace = std::make_shared<RowsTrace>();
    auto state = unique(schema, {a, b, a, c, b, d, d, e}, trace);
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    auto rows = drain(**state, *context);
    ASSERT_TRUE(rows);
    EXPECT_EQ(*rows, (std::vector<Row>{a, b, c, d, e}));
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(trace->count(RowsCall::kClose), 1U);
}

TEST(LabS06Q03Test, PullsOnlyEnoughRowsForTheNextDistinctResult) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    const Row a{types::Value::integer(3)};
    const Row b{types::Value::integer(8)};
    const Row c{types::Value::integer(9)};
    auto trace = std::make_shared<RowsTrace>();
    auto state = unique(int_schema(), {a, a, a, b, c}, trace);
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    EXPECT_EQ(trace->count(RowsCall::kOpen), 1U);
    EXPECT_EQ(trace->count(RowsCall::kNext), 0U);
    auto next = (*state)->next(*context);
    ASSERT_TRUE(next && *next);
    EXPECT_EQ(trace->count(RowsCall::kNext), 1U);
    EXPECT_EQ((*state)->output_slot().tuple()->values()[0U], a[0U]);
    next = (*state)->next(*context);
    ASSERT_TRUE(next && *next);
    EXPECT_EQ(trace->count(RowsCall::kNext), 4U);
    EXPECT_EQ((*state)->output_slot().tuple()->values()[0U], b[0U]);
    next = (*state)->next(*context);
    ASSERT_TRUE(next && *next);
    EXPECT_EQ(trace->count(RowsCall::kNext), 5U);
    EXPECT_EQ((*state)->output_slot().tuple()->values()[0U], c[0U]);
    next = (*state)->next(*context);
    ASSERT_TRUE(next);
    EXPECT_FALSE(*next);
    EXPECT_EQ(trace->count(RowsCall::kNext), 6U);
    ASSERT_TRUE((*state)->close(*context));
    for (const auto& event : trace->events) {
        EXPECT_EQ(event.context, &*context);
    }
}

TEST(LabS06Q03Test, UsesStructuralFloatBitsForSignedZeroAndNanPayloads) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    const planner::PlanSchema schema{
        .columns = {{.name = "value", .data_type = types::kFloat32Type}}
    };
    const Row positive_zero{types::Value::floating(0.0F)};
    const Row negative_zero{types::Value::floating(std::bit_cast<float>(0x8000'0000U))};
    const Row nan_a{types::Value::floating(std::bit_cast<float>(0x7fc0'0001U))};
    const Row nan_b{types::Value::floating(std::bit_cast<float>(0x7fc0'0002U))};
    const Row null{types::Value::null(types::kFloat32Type)};
    auto state = unique(
        schema,
        {positive_zero, negative_zero, positive_zero, nan_a, nan_a, nan_b, null, null},
        std::make_shared<RowsTrace>()
    );
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    auto rows = drain(**state, *context);
    ASSERT_TRUE(rows);
    EXPECT_EQ(*rows, (std::vector<Row>{positive_zero, negative_zero, nan_a, nan_b, null}));
    ASSERT_TRUE((*state)->close(*context));
}

TEST(LabS06Q03Test, RescanClearsSeenRowsAfterPartialAndCompleteConsumption) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    const Row a{types::Value::integer(4)};
    const Row b{types::Value::integer(6)};
    auto trace = std::make_shared<RowsTrace>();
    auto state = unique(int_schema(), {a, a, b}, trace);
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    auto first = (*state)->next(*context);
    ASSERT_TRUE(first && *first);
    for (std::size_t rescan = 1U; rescan <= 2U; ++rescan) {
        ASSERT_TRUE((*state)->rescan(*context));
        EXPECT_TRUE((*state)->output_slot().empty());
        auto rows = drain(**state, *context);
        ASSERT_TRUE(rows);
        EXPECT_EQ(*rows, (std::vector<Row>{a, b}));
        EXPECT_EQ(trace->count(RowsCall::kRescan), rescan);
    }
    ASSERT_TRUE((*state)->close(*context));
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(trace->count(RowsCall::kClose), 1U);
}

TEST(LabS06Q03Test, EmptyInputAndZeroColumnRowsHaveDifferentCardinality) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto empty = unique(int_schema(), {}, std::make_shared<RowsTrace>());
    ASSERT_TRUE(empty);
    ASSERT_TRUE((*empty)->open(*context));
    auto rows = drain(**empty, *context);
    ASSERT_TRUE(rows);
    EXPECT_TRUE(rows->empty());
    ASSERT_TRUE((*empty)->rescan(*context));
    rows = drain(**empty, *context);
    ASSERT_TRUE(rows);
    EXPECT_TRUE(rows->empty());
    ASSERT_TRUE((*empty)->close(*context));

    auto no_columns = unique({}, {Row{}, Row{}, Row{}}, std::make_shared<RowsTrace>());
    ASSERT_TRUE(no_columns);
    ASSERT_TRUE((*no_columns)->open(*context));
    rows = drain(**no_columns, *context);
    ASSERT_TRUE(rows);
    EXPECT_EQ(*rows, (std::vector<Row>{Row{}}));
    ASSERT_TRUE((*no_columns)->close(*context));
}

TEST(LabS06Q03Test, ChildOpenFailureIsPropagatedAndCleanupRunsOnce) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto trace = std::make_shared<RowsTrace>();
    auto state = unique(
        int_schema(),
        {},
        trace,
        {{RowsCall::kOpen, 1U, {base::ErrorCode::kIo, "scripted open"}}}
    );
    ASSERT_TRUE(state);
    auto failed = (*state)->open(*context);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(failed.error().message(), "scripted open");
    EXPECT_EQ((*state)->lifecycle(), PlanStateLifecycle::kFailed);
    EXPECT_EQ(trace->count(RowsCall::kOpen), 1U);
    EXPECT_EQ(trace->count(RowsCall::kClose), 0U);
    ASSERT_TRUE((*state)->close(*context));
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(trace->count(RowsCall::kClose), 1U);
}

TEST(LabS06Q03Test, NextFailureClearsOutputAndCannotBeRescanned) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto trace = std::make_shared<RowsTrace>();
    auto state = unique(
        int_schema(),
        {{types::Value::integer(1)}},
        trace,
        {{RowsCall::kNext, 2U, {base::ErrorCode::kIo, "scripted next"}}}
    );
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    auto first = (*state)->next(*context);
    ASSERT_TRUE(first && *first);
    auto failed = (*state)->next(*context);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().message(), "scripted next");
    EXPECT_TRUE((*state)->output_slot().empty());
    EXPECT_FALSE((*state)->rescan(*context));
    EXPECT_EQ(trace->count(RowsCall::kRescan), 0U);
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(trace->count(RowsCall::kClose), 1U);
}

TEST(LabS06Q03Test, RescanFailureIsPropagatedAndRemainsSafeToClose) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto trace = std::make_shared<RowsTrace>();
    auto state = unique(
        int_schema(),
        {{types::Value::integer(1)}},
        trace,
        {{RowsCall::kRescan, 1U, {base::ErrorCode::kIo, "scripted rescan"}}}
    );
    ASSERT_TRUE(state);
    ASSERT_TRUE((*state)->open(*context));
    auto first = (*state)->next(*context);
    ASSERT_TRUE(first && *first);
    auto failed = (*state)->rescan(*context);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().message(), "scripted rescan");
    EXPECT_TRUE((*state)->output_slot().empty());
    EXPECT_FALSE((*state)->next(*context));
    ASSERT_TRUE((*state)->close(*context));
    EXPECT_EQ(trace->count(RowsCall::kClose), 1U);
}

}  // namespace
}  // namespace db::execution
