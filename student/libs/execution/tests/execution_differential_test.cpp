#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/execution/aggregate.hpp"
#include "db/execution/limit.hpp"
#include "db/execution/nested_loop.hpp"
#include "db/execution/operators.hpp"
#include "db/execution/sort.hpp"
#include "db/execution/unique.hpp"
#include "db/planner/plan.hpp"
#include "db/types/value.hpp"

namespace db::execution {
namespace {

using planner::PlanExpressionPtr;

auto schema(const std::initializer_list<std::pair<const char*, types::DataType>> columns)
    -> planner::PlanSchema {
    planner::PlanSchema result;
    result.columns.reserve(columns.size());
    for (const auto& [name, data_type] : columns) {
        result.columns.push_back(planner::PlanOutputColumn{.name = name, .data_type = data_type});
    }
    return result;
}

auto literal(types::Value value) -> PlanExpressionPtr {
    const types::DataType data_type = value.data_type();
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanLiteralExpression{.value = std::move(value)}
    );
}

auto input(const std::uint32_t column, const types::DataType data_type) -> PlanExpressionPtr {
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanInputColumnReference{.column = planner::PlanColumnIndex{column}}
    );
}

auto binary(
    const types::BinaryOperator operation,
    const types::DataType data_type,
    PlanExpressionPtr left,
    PlanExpressionPtr right
) -> PlanExpressionPtr {
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanBinaryExpression{
            .op = operation,
            .left = std::move(left),
            .right = std::move(right),
        }
    );
}

auto boolean(std::vector<PlanExpressionPtr> operands) -> PlanExpressionPtr {
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        types::kBoolType,
        planner::PlanBooleanExpression{
            .op = types::BooleanOperator::kAnd,
            .operands = std::move(operands),
        }
    );
}

auto one_argument(PlanExpressionPtr expression) -> std::vector<PlanExpressionPtr> {
    std::vector<PlanExpressionPtr> result;
    result.push_back(std::move(expression));
    return result;
}

auto values_plan(const planner::PlanSchema& output, std::vector<std::vector<types::Value>> rows)
    -> planner::ValuesPlan {
    planner::ValuesPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .rows = {},
    };
    plan.rows.reserve(rows.size());
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

auto empty_context() -> base::Result<ExecContext> {
    auto parameters = planner::ParameterNamespace::from_external_slots({});
    if (!parameters) {
        return base::fail(std::move(parameters).error());
    }
    return ExecContext::create(parameters.value(), {});
}

auto read_rows(PlanState& state, ExecContext& context)
    -> base::Result<std::vector<std::vector<types::Value>>> {
    std::vector<std::vector<types::Value>> rows;
    while (true) {
        auto next = state.next(context);
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!next.value()) {
            return rows;
        }
        const Tuple* const tuple = state.output_slot().tuple();
        if (tuple == nullptr) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kExecution,
                    "execution differential state produced no output tuple",
                }
            );
        }
        rows.emplace_back(tuple->values().begin(), tuple->values().end());
    }
}

auto make_tuples(
    const planner::PlanSchema& input_schema,
    std::vector<std::vector<types::Value>> rows
) -> base::Result<std::vector<Tuple>> {
    auto descriptor = TupleDescriptor::from_plan_schema(input_schema);
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    std::vector<Tuple> result;
    result.reserve(rows.size());
    for (std::vector<types::Value>& values : rows) {
        auto tuple = Tuple::create(descriptor.value(), std::move(values));
        if (!tuple) {
            return base::fail(std::move(tuple).error());
        }
        result.push_back(std::move(tuple).value());
    }
    return result;
}

class VectorSeqPort final : public SeqScanPort {
public:
    explicit VectorSeqPort(std::vector<Tuple> rows)
        : rows_{std::move(rows)} {}

    auto configure(SeqScanRequest request) -> base::Status override {
        request_ = std::move(request);
        configured_ = true;
        return {};
    }

    auto open(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        if (!configured_) {
            return base::fail(base::Error{base::ErrorCode::kExecution, "scan is not configured"});
        }
        cursor_ = 0U;
        return {};
    }

    auto next(ExecContext& context, std::vector<types::Value> recycled_values)
        -> base::Result<std::optional<Tuple>> override {
        static_cast<void>(context);
        static_cast<void>(recycled_values);
        if (cursor_ == rows_.size()) {
            return std::optional<Tuple>{};
        }
        return std::optional<Tuple>{rows_[cursor_++]};
    }

    auto rescan(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        cursor_ = 0U;
        return {};
    }

    auto close(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        return {};
    }

private:
    std::vector<Tuple> rows_;
    SeqScanRequest request_;
    std::size_t cursor_{0U};
    bool configured_{false};
};

auto make_scan(
    const planner::PlanSchema& output,
    std::vector<std::vector<types::Value>> rows,
    PlanExpressionPtr filter
) -> base::Result<std::unique_ptr<SeqScanState>> {
    auto tuples = make_tuples(output, std::move(rows));
    if (!tuples) {
        return base::fail(std::move(tuples).error());
    }
    planner::SeqScanPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{101U},
                .name = "execution_corpus",
                .schema = output,
            },
        .required_columns = std::nullopt,
        .filter = std::move(filter),
    };
    auto port = std::make_unique<VectorSeqPort>(std::move(tuples).value());
    return SeqScanState::create(plan, std::move(port));
}

auto aggregate(
    const planner::PlanAggregateKind kind,
    std::vector<PlanExpressionPtr> arguments,
    const bool distinct,
    const bool star,
    const types::DataType result_type,
    const std::uint32_t index
) -> planner::PlanAggregate {
    return planner::PlanAggregate{
        .kind = kind,
        .arguments = std::move(arguments),
        .distinct = distinct,
        .star = star,
        .output_name = "aggregate_" + std::to_string(index),
        .data_type = result_type,
        .aggregate_index = planner::PlanAggregateIndex{index},
    };
}

auto joined_schema(const planner::PlanSchema& outer, const planner::PlanSchema& inner)
    -> planner::PlanSchema {
    planner::PlanSchema result{.columns = outer.columns};
    result.columns.insert(result.columns.end(), inner.columns.begin(), inner.columns.end());
    return result;
}

auto make_nested_loop(
    const planner::PlanSchema& outer_schema,
    std::vector<std::vector<types::Value>> outer_rows,
    const planner::PlanSchema& inner_schema,
    std::vector<std::vector<types::Value>> inner_rows,
    PlanExpressionPtr filter
) -> base::Result<std::unique_ptr<NestedLoopState>> {
    auto outer = ValuesState::create(values_plan(outer_schema, std::move(outer_rows)));
    if (!outer) {
        return base::fail(std::move(outer).error());
    }
    auto inner = ValuesState::create(values_plan(inner_schema, std::move(inner_rows)));
    if (!inner) {
        return base::fail(std::move(inner).error());
    }
    planner::NestedLoopPlan plan{
        .metadata =
            planner::PlanOperatorMetadata{
                .output = joined_schema(outer_schema, inner_schema),
            },
        .outer = nullptr,
        .inner = nullptr,
        .join_filter = std::move(filter),
        .parameters = {},
    };
    return NestedLoopState::create(plan, std::move(outer).value(), std::move(inner).value());
}

TEST(ExecutionDifferentialTest, AggregatesFilteredSignedAndFractionalRows) {
    const auto input_schema = schema({
        {"id", types::kInt64Type},
        {"int_value", types::kInt64Type},
        {"float_value", types::kFloat32Type},
    });
    auto scan = make_scan(
        input_schema,
        {
            {types::Value::integer(1), types::Value::integer(10), types::Value::floating(1.5F)},
            {types::Value::integer(2), types::Value::integer(-4), types::Value::floating(-0.5F)},
            {types::Value::integer(3), types::Value::integer(-6), types::Value::floating(0.25F)},
            {types::Value::integer(4), types::Value::integer(8), types::Value::floating(0.125F)},
            {types::Value::integer(5), types::Value::integer(0), types::Value::floating(-0.375F)},
        },
        binary(
            types::BinaryOperator::kLessEqual,
            types::kBoolType,
            input(0U, types::kInt64Type),
            literal(types::Value::integer(3))
        )
    );
    ASSERT_TRUE(scan) << scan.error().to_string();

    const auto output_schema = schema({
        {"aggregate_0", types::kInt64Type},
        {"aggregate_1", types::kFloat32Type},
    });
    planner::AggregatePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .group_by = {},
        .aggregates = {},
        .having = nullptr,
        .ordered_min = false,
    };
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kSum,
        one_argument(input(1U, types::kInt64Type)),
        false,
        false,
        types::kInt64Type,
        0U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kSum,
        one_argument(input(2U, types::kFloat32Type)),
        false,
        false,
        types::kFloat32Type,
        1U
    ));
    auto state = AggregateState::create(plan, std::move(scan).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(
        rows.value(),
        (std::vector<std::vector<types::Value>>{{
            types::Value::integer(0),
            types::Value::floating(1.25F),
        }})
    );
}

TEST(ExecutionDifferentialTest, CountAndExtremaIgnoreTypedNulls) {
    const auto input_schema = schema({
        {"id", types::kInt64Type},
        {"score", types::kFloat32Type},
    });
    auto child = ValuesState::create(values_plan(
        input_schema,
        {
            {types::Value::integer(5), types::Value::floating(-2.5F)},
            {types::Value::integer(1), types::Value::floating(0.0F)},
            {types::Value::integer(9), types::Value::null(types::kFloat32Type)},
            {types::Value::integer(3), types::Value::floating(8.25F)},
        }
    ));
    ASSERT_TRUE(child) << child.error().to_string();
    const auto output_schema = schema({
        {"aggregate_0", types::kInt64Type},
        {"aggregate_1", types::kInt64Type},
        {"aggregate_2", types::kInt64Type},
        {"aggregate_3", types::kInt64Type},
        {"aggregate_4", types::kFloat32Type},
        {"aggregate_5", types::kFloat32Type},
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
        one_argument(input(1U, types::kFloat32Type)),
        false,
        false,
        types::kInt64Type,
        1U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kMinimum,
        one_argument(input(0U, types::kInt64Type)),
        false,
        false,
        types::kInt64Type,
        2U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kMaximum,
        one_argument(input(0U, types::kInt64Type)),
        false,
        false,
        types::kInt64Type,
        3U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kMinimum,
        one_argument(input(1U, types::kFloat32Type)),
        false,
        false,
        types::kFloat32Type,
        4U
    ));
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kMaximum,
        one_argument(input(1U, types::kFloat32Type)),
        false,
        false,
        types::kFloat32Type,
        5U
    ));
    auto state = AggregateState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(
        rows.value(),
        (std::vector<std::vector<types::Value>>{{
            types::Value::integer(4),
            types::Value::integer(3),
            types::Value::integer(1),
            types::Value::integer(9),
            types::Value::floating(-2.5F),
            types::Value::floating(8.25F),
        }})
    );
}

TEST(ExecutionDifferentialTest, JoinFanoutFeedsDistinctAggregate) {
    const auto orders = schema({
        {"w_id", types::kInt64Type},
        {"d_id", types::kInt64Type},
        {"o_id", types::kInt64Type},
        {"item_id", types::kInt64Type},
    });
    const auto stock = schema({
        {"w_id", types::kInt64Type},
        {"item_id", types::kInt64Type},
        {"quantity", types::kInt64Type},
    });
    const std::vector<std::vector<types::Value>> order_rows{
        {types::Value::integer(1),
         types::Value::integer(1),
         types::Value::integer(5),
         types::Value::integer(11)},
        {types::Value::integer(1),
         types::Value::integer(1),
         types::Value::integer(5),
         types::Value::integer(11)},
        {types::Value::integer(1),
         types::Value::integer(1),
         types::Value::integer(6),
         types::Value::integer(12)},
        {types::Value::integer(1),
         types::Value::integer(1),
         types::Value::integer(7),
         types::Value::integer(13)},
        {types::Value::integer(2),
         types::Value::integer(1),
         types::Value::integer(5),
         types::Value::integer(99)},
    };
    const std::vector<std::vector<types::Value>> stock_rows{
        {types::Value::integer(1), types::Value::integer(11), types::Value::integer(9)},
        {types::Value::integer(1), types::Value::integer(11), types::Value::integer(9)},
        {types::Value::integer(1), types::Value::integer(12), types::Value::integer(10)},
        {types::Value::integer(1), types::Value::integer(13), types::Value::integer(4)},
        {types::Value::integer(2), types::Value::integer(99), types::Value::integer(1)},
    };
    std::vector<PlanExpressionPtr> predicates;
    predicates.push_back(binary(
        types::BinaryOperator::kEqual,
        types::kBoolType,
        input(0U, types::kInt64Type),
        literal(types::Value::integer(1))
    ));
    predicates.push_back(binary(
        types::BinaryOperator::kEqual,
        types::kBoolType,
        input(1U, types::kInt64Type),
        literal(types::Value::integer(1))
    ));
    predicates.push_back(binary(
        types::BinaryOperator::kGreaterEqual,
        types::kBoolType,
        input(2U, types::kInt64Type),
        literal(types::Value::integer(5))
    ));
    predicates.push_back(binary(
        types::BinaryOperator::kLess,
        types::kBoolType,
        input(2U, types::kInt64Type),
        literal(types::Value::integer(25))
    ));
    predicates.push_back(binary(
        types::BinaryOperator::kEqual,
        types::kBoolType,
        input(4U, types::kInt64Type),
        input(0U, types::kInt64Type)
    ));
    predicates.push_back(binary(
        types::BinaryOperator::kEqual,
        types::kBoolType,
        input(5U, types::kInt64Type),
        input(3U, types::kInt64Type)
    ));
    auto join =
        make_nested_loop(orders, order_rows, stock, stock_rows, boolean(std::move(predicates)));
    ASSERT_TRUE(join) << join.error().to_string();
    const auto output_schema = schema({{"aggregate_0", types::kInt64Type}});
    planner::AggregatePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .group_by = {},
        .aggregates = {},
        .having = nullptr,
        .ordered_min = false,
    };
    plan.aggregates.push_back(aggregate(
        planner::PlanAggregateKind::kCount,
        one_argument(input(3U, types::kInt64Type)),
        true,
        false,
        types::kInt64Type,
        0U
    ));
    auto state = AggregateState::create(plan, std::move(join).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(rows.value(), (std::vector<std::vector<types::Value>>{{types::Value::integer(3)}}));
}

TEST(ExecutionDifferentialTest, SortLimitProjectionPreservesStableMultiKeyOrder) {
    const auto input_schema = schema({
        {"id", types::kInt64Type},
        {"team", types::kInt64Type},
        {"score", types::kFloat32Type},
        {"bucket", types::kInt64Type},
    });
    auto child = ValuesState::create(values_plan(
        input_schema,
        {
            {types::Value::integer(1),
             types::Value::integer(1),
             types::Value::floating(90.0F),
             types::Value::integer(2)},
            {types::Value::integer(2),
             types::Value::integer(0),
             types::Value::floating(95.0F),
             types::Value::integer(1)},
            {types::Value::integer(3),
             types::Value::integer(2),
             types::Value::floating(95.0F),
             types::Value::integer(1)},
            {types::Value::integer(4),
             types::Value::integer(0),
             types::Value::floating(95.0F),
             types::Value::integer(1)},
            {types::Value::integer(5),
             types::Value::integer(3),
             types::Value::floating(-1.0F),
             types::Value::integer(3)},
            {types::Value::integer(6),
             types::Value::integer(1),
             types::Value::floating(90.0F),
             types::Value::integer(1)},
            {types::Value::integer(7),
             types::Value::integer(0),
             types::Value::floating(0.0F),
             types::Value::integer(2)},
            {types::Value::integer(8),
             types::Value::integer(2),
             types::Value::floating(90.0F),
             types::Value::integer(1)},
        }
    ));
    ASSERT_TRUE(child) << child.error().to_string();
    planner::SortPlan sort_plan{
        .metadata = planner::PlanOperatorMetadata{.output = input_schema},
        .input = nullptr,
        .order_by = {},
    };
    sort_plan.order_by.push_back({
        .expression = input(2U, types::kFloat32Type),
        .direction = planner::PlanSortDirection::kDescending,
    });
    sort_plan.order_by.push_back({
        .expression = input(1U, types::kInt64Type),
        .direction = planner::PlanSortDirection::kAscending,
    });
    sort_plan.order_by.push_back({
        .expression = input(3U, types::kInt64Type),
        .direction = planner::PlanSortDirection::kDescending,
    });
    sort_plan.order_by.push_back({
        .expression = input(0U, types::kInt64Type),
        .direction = planner::PlanSortDirection::kDescending,
    });
    auto sorted = SortState::create(sort_plan, std::move(child).value());
    ASSERT_TRUE(sorted) << sorted.error().to_string();
    planner::LimitPlan limit_plan{
        .metadata = planner::PlanOperatorMetadata{.output = input_schema},
        .input = nullptr,
        .limit = literal(types::Value::integer(5)),
        .offset = nullptr,
    };
    auto limited = LimitState::create(limit_plan, std::move(sorted).value());
    ASSERT_TRUE(limited) << limited.error().to_string();
    const auto output_schema = schema({{"id", types::kInt64Type}});
    planner::ResultPlan result_plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .projection = {},
    };
    result_plan.projection.push_back(
        {.expression = input(0U, types::kInt64Type), .output_name = "id"}
    );
    auto result = ResultState::create(result_plan, std::move(limited).value());
    ASSERT_TRUE(result) << result.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(result.value()->open(context.value()));
    auto rows = read_rows(*result.value(), context.value());
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(
        rows.value(),
        (std::vector<std::vector<types::Value>>{
            {types::Value::integer(4)},
            {types::Value::integer(2)},
            {types::Value::integer(3)},
            {types::Value::integer(1)},
            {types::Value::integer(6)},
        })
    );
}

TEST(ExecutionDifferentialTest, InequalityJoinKeepsStrictBoundsAndDeterministicOrder) {
    const auto outer_schema = schema({{"id", types::kInt64Type}});
    const auto inner_schema = schema({{"item_id", types::kInt64Type}});
    std::vector<PlanExpressionPtr> join_predicates;
    join_predicates.push_back(binary(
        types::BinaryOperator::kGreater,
        types::kBoolType,
        input(1U, types::kInt64Type),
        input(0U, types::kInt64Type)
    ));
    join_predicates.push_back(binary(
        types::BinaryOperator::kGreater,
        types::kBoolType,
        input(0U, types::kInt64Type),
        literal(types::Value::integer(1))
    ));
    join_predicates.push_back(binary(
        types::BinaryOperator::kLess,
        types::kBoolType,
        input(0U, types::kInt64Type),
        literal(types::Value::integer(4))
    ));
    auto join = make_nested_loop(
        outer_schema,
        {{types::Value::integer(1)},
         {types::Value::integer(2)},
         {types::Value::integer(3)},
         {types::Value::integer(4)}},
        inner_schema,
        {{types::Value::integer(2)},
         {types::Value::integer(3)},
         {types::Value::integer(4)},
         {types::Value::integer(5)},
         {types::Value::integer(6)}},
        boolean(std::move(join_predicates))
    );
    ASSERT_TRUE(join) << join.error().to_string();
    const auto output_schema = joined_schema(outer_schema, inner_schema);
    planner::SortPlan sort_plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .order_by = {},
    };
    sort_plan.order_by.push_back({.expression = input(1U, types::kInt64Type)});
    sort_plan.order_by.push_back({.expression = input(0U, types::kInt64Type)});
    auto sorted = SortState::create(sort_plan, std::move(join).value());
    ASSERT_TRUE(sorted) << sorted.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(sorted.value()->open(context.value()));
    auto rows = read_rows(*sorted.value(), context.value());
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(
        rows.value(),
        (std::vector<std::vector<types::Value>>{
            {types::Value::integer(2), types::Value::integer(3)},
            {types::Value::integer(2), types::Value::integer(4)},
            {types::Value::integer(3), types::Value::integer(4)},
            {types::Value::integer(2), types::Value::integer(5)},
            {types::Value::integer(3), types::Value::integer(5)},
            {types::Value::integer(2), types::Value::integer(6)},
            {types::Value::integer(3), types::Value::integer(6)},
        })
    );
}

TEST(ExecutionDifferentialTest, NullQualificationFeedsFirstValueUnique) {
    const auto input_schema = schema({{"value", types::kInt64Type}});
    auto scan = make_scan(
        input_schema,
        {{types::Value::integer(1)},
         {types::Value::null(types::kInt64Type)},
         {types::Value::integer(1)},
         {types::Value::integer(2)}},
        binary(
            types::BinaryOperator::kGreaterEqual,
            types::kBoolType,
            input(0U, types::kInt64Type),
            literal(types::Value::integer(1))
        )
    );
    ASSERT_TRUE(scan) << scan.error().to_string();
    planner::UniquePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = input_schema},
        .input = nullptr,
    };
    auto state = UniqueState::create(plan, std::move(scan).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(
        rows.value(),
        (std::vector<std::vector<types::Value>>{
            {types::Value::integer(1)},
            {types::Value::integer(2)},
        })
    );
}

}  // namespace
}  // namespace db::execution
