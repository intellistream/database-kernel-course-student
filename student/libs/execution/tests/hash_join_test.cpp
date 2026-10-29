#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/execution/hash_join.hpp"
#include "db/execution/operators.hpp"
#include "db/planner/plan.hpp"
#include "db/types/value.hpp"

namespace db::execution {
namespace {

using planner::PlanExpression;
using planner::PlanExpressionPtr;

auto literal_value(types::Value value) -> PlanExpressionPtr {
    const auto data_type = value.data_type();
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanLiteralExpression{.value = std::move(value)}
    );
}

auto input(const std::uint32_t column) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kInt64Type,
        planner::PlanInputColumnReference{.column = planner::PlanColumnIndex{column}}
    );
}

auto equal(PlanExpressionPtr left, PlanExpressionPtr right) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kBoolType,
        planner::PlanBinaryExpression{
            .op = types::BinaryOperator::kEqual,
            .left = std::move(left),
            .right = std::move(right),
        }
    );
}

auto schema(std::initializer_list<const char*> names) -> planner::PlanSchema {
    planner::PlanSchema result;
    for (const char* name : names) {
        result.columns.push_back({.name = name, .data_type = types::kInt64Type});
    }
    return result;
}

auto joined_schema(const planner::PlanSchema& outer, const planner::PlanSchema& inner)
    -> planner::PlanSchema {
    planner::PlanSchema result{.columns = outer.columns};
    result.columns.insert(result.columns.end(), inner.columns.begin(), inner.columns.end());
    return result;
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
            expressions.push_back(literal_value(std::move(value)));
        }
        plan.rows.push_back(std::move(expressions));
    }
    return plan;
}

auto empty_context() -> base::Result<ExecContext> {
    auto namespace_result = planner::ParameterNamespace::from_external_slots({});
    if (!namespace_result) {
        return base::fail(std::move(namespace_result).error());
    }
    return ExecContext::create(namespace_result.value(), {});
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
                    "hash-join test state produced no output tuple",
                }
            );
        }
        rows.emplace_back(tuple->values().begin(), tuple->values().end());
    }
}

auto make_join(
    const planner::PlanSchema& outer_schema,
    const planner::PlanSchema& inner_schema,
    std::vector<std::vector<types::Value>> outer_rows,
    std::vector<std::vector<types::Value>> inner_rows,
    std::vector<planner::PlanJoinColumnPair> keys,
    PlanExpressionPtr residual = nullptr
) -> base::Result<std::unique_ptr<HashJoinState>> {
    auto outer_plan = values_plan(outer_schema, std::move(outer_rows));
    auto inner_plan = values_plan(inner_schema, std::move(inner_rows));
    auto outer = ValuesState::create(outer_plan);
    auto inner = ValuesState::create(inner_plan);
    if (!outer) {
        return base::fail(std::move(outer).error());
    }
    if (!inner) {
        return base::fail(std::move(inner).error());
    }
    planner::HashJoinPlan plan{
        .metadata =
            planner::PlanOperatorMetadata{.output = joined_schema(outer_schema, inner_schema)},
        .outer = nullptr,
        .inner = nullptr,
        .join_columns = std::move(keys),
        .residual_filter = std::move(residual),
    };
    return HashJoinState::create(plan, std::move(outer).value(), std::move(inner).value());
}

TEST(HashJoinStateTest, EmitsOuterMajorAndInnerBuildOrderAndRescans) {
    const auto outer_schema = schema({"outer_id"});
    const auto inner_schema = schema({"inner_id"});
    auto state = make_join(
        outer_schema,
        inner_schema,
        {{types::Value::integer(2)}, {types::Value::integer(1)}, {types::Value::integer(2)}},
        {{types::Value::integer(1)}, {types::Value::integer(2)}, {types::Value::integer(2)}},
        {{.outer_column = planner::PlanColumnIndex{0U},
          .inner_column = planner::PlanColumnIndex{0U}}}
    );
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    auto first = read_rows(*state.value(), context.value());
    ASSERT_TRUE(first) << first.error().to_string();
    ASSERT_EQ(first->size(), 5U);
    EXPECT_EQ(
        first->at(0U),
        (std::vector<types::Value>{types::Value::integer(2), types::Value::integer(2)})
    );
    EXPECT_EQ(
        first->at(1U),
        (std::vector<types::Value>{types::Value::integer(2), types::Value::integer(2)})
    );
    EXPECT_EQ(
        first->at(2U),
        (std::vector<types::Value>{types::Value::integer(1), types::Value::integer(1)})
    );
    EXPECT_EQ(
        first->at(3U),
        (std::vector<types::Value>{types::Value::integer(2), types::Value::integer(2)})
    );
    EXPECT_EQ(
        first->at(4U),
        (std::vector<types::Value>{types::Value::integer(2), types::Value::integer(2)})
    );
    ASSERT_TRUE(state.value()->rescan(context.value()));
    auto second = read_rows(*state.value(), context.value());
    ASSERT_TRUE(second);
    EXPECT_EQ(second.value(), first.value());
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(HashJoinStateTest, SkipsNullKeysAndRequiresTrueResidualQualification) {
    const auto outer_schema = schema({"outer_id"});
    const auto inner_schema = schema({"inner_id", "payload"});
    auto residual = equal(input(2U), literal_value(types::Value::integer(10)));
    auto state = make_join(
        outer_schema,
        inner_schema,
        {{types::Value::integer(1)}, {types::Value::null(types::kInt64Type)}},
        {
            {types::Value::integer(1), types::Value::integer(10)},
            {types::Value::integer(1), types::Value::integer(11)},
            {types::Value::integer(1), types::Value::null(types::kInt64Type)},
            {types::Value::null(types::kInt64Type), types::Value::integer(10)},
        },
        {{.outer_column = planner::PlanColumnIndex{0U},
          .inner_column = planner::PlanColumnIndex{0U}}},
        std::move(residual)
    );
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows);
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(
        rows->front(),
        (std::vector<types::Value>{
            types::Value::integer(1),
            types::Value::integer(1),
            types::Value::integer(10),
        })
    );
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(HashJoinStateTest, SupportsCompositeKeysAndRejectsKeylessPlans) {
    const auto outer_schema = schema({"a", "b"});
    const auto inner_schema = schema({"x", "y"});
    auto state = make_join(
        outer_schema,
        inner_schema,
        {{types::Value::integer(1), types::Value::integer(2)},
         {types::Value::integer(1), types::Value::integer(3)}},
        {{types::Value::integer(1), types::Value::integer(3)},
         {types::Value::integer(1), types::Value::integer(2)},
         {types::Value::integer(1), types::Value::integer(2)}},
        {{.outer_column = planner::PlanColumnIndex{0U},
          .inner_column = planner::PlanColumnIndex{0U}},
         {.outer_column = planner::PlanColumnIndex{1U},
          .inner_column = planner::PlanColumnIndex{1U}}}
    );
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows);
    ASSERT_EQ(rows->size(), 3U);
    EXPECT_EQ(
        rows->at(0U),
        (std::vector<types::Value>{
            types::Value::integer(1),
            types::Value::integer(2),
            types::Value::integer(1),
            types::Value::integer(2)
        })
    );
    EXPECT_EQ(
        rows->at(1U),
        (std::vector<types::Value>{
            types::Value::integer(1),
            types::Value::integer(2),
            types::Value::integer(1),
            types::Value::integer(2)
        })
    );
    EXPECT_EQ(
        rows->at(2U),
        (std::vector<types::Value>{
            types::Value::integer(1),
            types::Value::integer(3),
            types::Value::integer(1),
            types::Value::integer(3)
        })
    );
    ASSERT_TRUE(state.value()->close(context.value()));

    auto keyless = make_join(outer_schema, inner_schema, {}, {}, {});
    ASSERT_FALSE(keyless);
    EXPECT_EQ(keyless.error().code(), base::ErrorCode::kPlan);

    auto mismatched_inner = inner_schema;
    mismatched_inner.columns[0].data_type = types::kBoolType;
    auto mismatched = make_join(
        outer_schema,
        mismatched_inner,
        {},
        {},
        {{.outer_column = planner::PlanColumnIndex{0U},
          .inner_column = planner::PlanColumnIndex{0U}}}
    );
    ASSERT_FALSE(mismatched);
    EXPECT_EQ(mismatched.error().code(), base::ErrorCode::kPlan);
}

}  // namespace
}  // namespace db::execution
