#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/execution/aggregate.hpp"
#include "db/execution/operators.hpp"
#include "db/planner/plan.hpp"
#include "db/types/value.hpp"

namespace db::execution {
namespace {

using planner::PlanExpression;
using planner::PlanExpressionPtr;

auto literal(types::Value value) -> PlanExpressionPtr {
    const auto data_type = value.data_type();
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanLiteralExpression{.value = std::move(value)}
    );
}

auto input_column(const std::uint32_t index, const types::DataType type) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        type,
        planner::PlanInputColumnReference{.column = planner::PlanColumnIndex{index}}
    );
}

auto one_argument(PlanExpressionPtr expression) -> std::vector<PlanExpressionPtr> {
    std::vector<PlanExpressionPtr> arguments;
    arguments.push_back(std::move(expression));
    return arguments;
}

auto greater_than_input(
    const std::uint32_t index,
    const types::DataType input_type,
    const std::int64_t value
) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kBoolType,
        planner::PlanBinaryExpression{
            .op = types::BinaryOperator::kGreater,
            .left = input_column(index, input_type),
            .right = literal(types::Value::integer(value)),
        }
    );
}

auto schema(std::vector<planner::PlanOutputColumn> columns) -> planner::PlanSchema {
    return planner::PlanSchema{.columns = std::move(columns)};
}

auto values_plan(const planner::PlanSchema& output, std::vector<std::vector<types::Value>> rows)
    -> planner::ValuesPlan {
    planner::ValuesPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .rows = {},
    };
    for (std::vector<types::Value>& values : rows) {
        std::vector<PlanExpressionPtr> expressions;
        expressions.reserve(values.size());
        for (types::Value& value : values) {
            expressions.push_back(literal(std::move(value)));
        }
        plan.rows.push_back(std::move(expressions));
    }
    return plan;
}

auto aggregate(
    const planner::PlanAggregateKind kind,
    std::vector<PlanExpressionPtr> arguments,
    const bool distinct,
    const bool star,
    const types::DataType result_type,
    [[maybe_unused]] const std::uint32_t index
) -> planner::PlanAggregate {
    std::string output_name;
    switch (kind) {
        case planner::PlanAggregateKind::kCount:
            output_name = "count";
            break;
        case planner::PlanAggregateKind::kSum:
            output_name = "sum";
            break;
        case planner::PlanAggregateKind::kAverage:
            output_name = "avg";
            break;
        case planner::PlanAggregateKind::kMinimum:
            output_name = "min";
            break;
        case planner::PlanAggregateKind::kMaximum:
            output_name = "max";
            break;
    }
    return planner::PlanAggregate{
        .kind = kind,
        .arguments = std::move(arguments),
        .distinct = distinct,
        .star = star,
        .output_name = std::move(output_name),
        .data_type = result_type,
        .aggregate_index = planner::PlanAggregateIndex{index},
    };
}

auto empty_context() -> base::Result<ExecContext> {
    auto parameters = planner::ParameterNamespace::from_external_slots({});
    if (!parameters) {
        return base::fail(std::move(parameters).error());
    }
    return ExecContext::create(parameters.value(), {});
}

auto read_rows(PlanState& state, ExecContext& context) -> std::vector<std::vector<types::Value>> {
    std::vector<std::vector<types::Value>> rows;
    while (true) {
        auto next = state.next(context);
        EXPECT_TRUE(next) << next.error().to_string();
        if (!next || !next.value()) {
            break;
        }
        const Tuple* const tuple = state.output_slot().tuple();
        if (tuple == nullptr) {
            ADD_FAILURE() << "aggregate state reported a row without output";
            break;
        }
        rows.emplace_back(tuple->values().begin(), tuple->values().end());
    }
    return rows;
}

TEST(AggregateStateTest, ComputesScalarAggregatesNullsAndRescan) {
    const auto input_schema = schema({
        {.name = "id", .data_type = types::kInt64Type},
        {.name = "score", .data_type = types::kFloat32Type},
    });
    auto child = ValuesState::create(values_plan(
        input_schema,
        {
            {types::Value::null(types::kInt64Type), types::Value::floating(1.0F)},
            {types::Value::integer(1), types::Value::floating(2.0F)},
            {types::Value::integer(2), types::Value::floating(3.0F)},
        }
    ));
    ASSERT_TRUE(child) << child.error().to_string();

    const auto output_schema = schema({
        {.name = "count", .data_type = types::kInt64Type},
        {.name = "count", .data_type = types::kInt64Type},
        {.name = "sum", .data_type = types::kInt64Type},
        {.name = "avg", .data_type = types::kFloat32Type},
        {.name = "min", .data_type = types::kInt64Type},
        {.name = "max", .data_type = types::kFloat32Type},
    });
    planner::AggregatePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .group_by = {},
        .aggregates = {},
        .having = nullptr,
        .ordered_min = false,
    };
    plan.aggregates.push_back(
        aggregate(planner::PlanAggregateKind::kCount, {}, false, true, types::kInt64Type, 0U)
    );
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kCount,
        one_argument(input_column(0U, types::kInt64Type)),
        false,
        false,
        types::kInt64Type,
        1U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kSum,
        one_argument(input_column(0U, types::kInt64Type)),
        false,
        false,
        types::kInt64Type,
        2U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kAverage,
        one_argument(input_column(1U, types::kFloat32Type)),
        false,
        false,
        types::kFloat32Type,
        3U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kMinimum,
        one_argument(input_column(0U, types::kInt64Type)),
        false,
        false,
        types::kInt64Type,
        4U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kMaximum,
        one_argument(input_column(1U, types::kFloat32Type)),
        false,
        false,
        types::kFloat32Type,
        5U
    ));

    auto state = AggregateState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    EXPECT_EQ(
        read_rows(*state.value(), context.value()),
        (std::vector<std::vector<types::Value>>{{
            types::Value::integer(3),
            types::Value::integer(2),
            types::Value::integer(3),
            types::Value::floating(2.0F),
            types::Value::integer(1),
            types::Value::floating(3.0F),
        }})
    );
    ASSERT_TRUE(state.value()->rescan(context.value()));
    EXPECT_EQ(read_rows(*state.value(), context.value()).size(), 1U);
}

TEST(AggregateStateTest, PreservesFirstSeenGroupOrderAndDistinctValues) {
    const auto input_schema = schema({
        {.name = "group", .data_type = types::kInt64Type},
        {.name = "value", .data_type = types::kInt64Type},
    });
    auto child = ValuesState::create(values_plan(
        input_schema,
        {
            {types::Value::integer(2), types::Value::integer(1)},
            {types::Value::integer(1), types::Value::integer(1)},
            {types::Value::integer(2), types::Value::integer(1)},
            {types::Value::integer(1), types::Value::integer(3)},
        }
    ));
    ASSERT_TRUE(child);
    const auto output_schema = schema({
        {.name = "group", .data_type = types::kInt64Type},
        {.name = "count", .data_type = types::kInt64Type},
        {.name = "sum", .data_type = types::kInt64Type},
    });
    planner::AggregatePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .group_by = one_argument(input_column(0U, types::kInt64Type)),
        .aggregates = {},
        .having = nullptr,
        .ordered_min = false,
    };
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kCount,
        one_argument(input_column(1U, types::kInt64Type)),
        true,
        false,
        types::kInt64Type,
        0U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kSum,
        one_argument(input_column(1U, types::kInt64Type)),
        true,
        false,
        types::kInt64Type,
        1U
    ));
    auto state = AggregateState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    EXPECT_EQ(
        read_rows(*state.value(), context.value()),
        (std::vector<std::vector<types::Value>>{
            {types::Value::integer(2), types::Value::integer(1), types::Value::integer(1)},
            {types::Value::integer(1), types::Value::integer(2), types::Value::integer(4)},
        })
    );
}

TEST(AggregateStateTest, PreservesFirstSeenOrderAcrossGroupIndexGrowth) {
    constexpr std::size_t group_count = 257U;
    const auto input_schema = schema({
        {.name = "group", .data_type = types::kInt64Type},
    });
    std::vector<std::vector<types::Value>> input_rows;
    std::vector<std::vector<types::Value>> expected_rows;
    input_rows.reserve(group_count);
    expected_rows.reserve(group_count);
    for (std::size_t index = 0U; index < group_count; ++index) {
        const auto key = static_cast<std::int64_t>((index * 73U) % group_count);
        input_rows.push_back({types::Value::integer(key)});
        expected_rows.push_back({types::Value::integer(key), types::Value::integer(1)});
    }
    auto child = ValuesState::create(values_plan(input_schema, std::move(input_rows)));
    ASSERT_TRUE(child) << child.error().to_string();

    const auto output_schema = schema({
        {.name = "group", .data_type = types::kInt64Type},
        {.name = "count", .data_type = types::kInt64Type},
    });
    planner::AggregatePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .group_by = one_argument(input_column(0U, types::kInt64Type)),
        .aggregates = {},
        .having = nullptr,
        .ordered_min = false,
    };
    plan.aggregates.push_back(
        aggregate(planner::PlanAggregateKind::kCount, {}, false, true, types::kInt64Type, 0U)
    );
    auto state = AggregateState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    EXPECT_EQ(read_rows(*state.value(), context.value()), expected_rows);
}

TEST(AggregateStateTest, AppliesHavingAndUsesReferenceFloatAccumulationRules) {
    const auto input_schema = schema({
        {.name = "group", .data_type = types::kInt64Type},
        {.name = "value", .data_type = types::kFloat32Type},
    });
    auto child = ValuesState::create(values_plan(
        input_schema,
        {
            {types::Value::integer(1), types::Value::floating(16'777'216.0F)},
            {types::Value::integer(1), types::Value::floating(1.0F)},
            {types::Value::integer(1), types::Value::floating(1.0F)},
            {types::Value::integer(2), types::Value::floating(1.0F)},
        }
    ));
    ASSERT_TRUE(child);
    const auto output_schema = schema({
        {.name = "group", .data_type = types::kInt64Type},
        {.name = "sum", .data_type = types::kFloat32Type},
        {.name = "avg", .data_type = types::kFloat32Type},
    });
    planner::AggregatePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .group_by = one_argument(input_column(0U, types::kInt64Type)),
        .aggregates = {},
        .having = greater_than_input(0U, types::kInt64Type, 1),
        .ordered_min = false,
    };
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kSum,
        one_argument(input_column(1U, types::kFloat32Type)),
        false,
        false,
        types::kFloat32Type,
        0U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kAverage,
        one_argument(input_column(1U, types::kFloat32Type)),
        false,
        false,
        types::kFloat32Type,
        1U
    ));
    auto state = AggregateState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    EXPECT_EQ(
        read_rows(*state.value(), context.value()),
        (std::vector<std::vector<types::Value>>{{
            types::Value::integer(2),
            types::Value::floating(1.0F),
            types::Value::floating(1.0F),
        }})
    );
}

TEST(AggregateStateTest, ScalarEmptyInputProducesOneRowAndGroupedEmptyProducesNone) {
    const auto input_schema = schema({
        {.name = "value", .data_type = types::kInt64Type},
    });
    auto make_plan = [](std::vector<PlanExpressionPtr> group_by) {
        std::vector<planner::PlanOutputColumn> columns;
        if (!group_by.empty()) {
            columns.push_back({.name = "group", .data_type = types::kInt64Type});
        }
        columns.push_back({.name = "count", .data_type = types::kInt64Type});
        columns.push_back({.name = "sum", .data_type = types::kInt64Type});
        planner::AggregatePlan plan{
            .metadata = planner::PlanOperatorMetadata{.output = schema(std::move(columns))},
            .input = nullptr,
            .group_by = std::move(group_by),
            .aggregates = {},
            .having = nullptr,
            .ordered_min = false,
        };
        plan.aggregates.push_back(aggregate(
            planner::PlanAggregateKind::kCount,
            one_argument(input_column(0U, types::kInt64Type)),
            false,
            false,
            types::kInt64Type,
            0U
        ));
        plan.aggregates.push_back(aggregate(
            planner::PlanAggregateKind::kSum,
            one_argument(input_column(0U, types::kInt64Type)),
            false,
            false,
            types::kInt64Type,
            1U
        ));
        return plan;
    };

    auto scalar_child = ValuesState::create(values_plan(input_schema, {}));
    ASSERT_TRUE(scalar_child);
    auto scalar = AggregateState::create(make_plan({}), std::move(scalar_child).value());
    ASSERT_TRUE(scalar);
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(scalar.value()->open(context.value()));
    EXPECT_EQ(
        read_rows(*scalar.value(), context.value()),
        (std::vector<std::vector<types::Value>>{{
            types::Value::integer(0),
            types::Value::null(types::kInt64Type),
        }})
    );

    auto grouped_child = ValuesState::create(values_plan(input_schema, {}));
    ASSERT_TRUE(grouped_child);
    auto grouped = AggregateState::create(
        make_plan(one_argument(input_column(0U, types::kInt64Type))),
        std::move(grouped_child).value()
    );
    ASSERT_TRUE(grouped);
    ASSERT_TRUE(grouped.value()->open(context.value()));
    EXPECT_TRUE(read_rows(*grouped.value(), context.value()).empty());
}

TEST(AggregateStateTest, RejectsInvalidAggregateMetadata) {
    const auto input_schema = schema({
        {.name = "value", .data_type = types::kInt64Type},
    });
    auto child = ValuesState::create(values_plan(input_schema, {}));
    ASSERT_TRUE(child);
    const auto output_schema = schema({
        {.name = "sum", .data_type = types::kInt64Type},
    });
    planner::AggregatePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .group_by = {},
        .aggregates = {},
        .having = nullptr,
        .ordered_min = false,
    };
    plan.aggregates.push_back(
        aggregate(planner::PlanAggregateKind::kSum, {}, false, false, types::kInt64Type, 0U)
    );
    auto state = AggregateState::create(plan, std::move(child).value());
    ASSERT_FALSE(state);
    EXPECT_EQ(state.error().code(), base::ErrorCode::kPlan);
}

TEST(AggregateStateTest, RejectsDistinctCountStar) {
    const auto input_schema = schema({
        {.name = "value", .data_type = types::kInt64Type},
    });
    auto child = ValuesState::create(values_plan(input_schema, {}));
    ASSERT_TRUE(child);
    const auto output_schema = schema({
        {.name = "count", .data_type = types::kInt64Type},
    });
    planner::AggregatePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .group_by = {},
        .aggregates = {},
        .having = nullptr,
        .ordered_min = false,
    };
    plan.aggregates.push_back(
        aggregate(planner::PlanAggregateKind::kCount, {}, true, true, types::kInt64Type, 0U)
    );
    auto state = AggregateState::create(plan, std::move(child).value());
    ASSERT_FALSE(state);
    EXPECT_EQ(state.error().code(), base::ErrorCode::kPlan);
}

}  // namespace
}  // namespace db::execution
