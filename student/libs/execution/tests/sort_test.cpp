#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/execution/operators.hpp"
#include "db/execution/sort.hpp"
#include "db/execution/sort_support.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::execution {
namespace {

using planner::PlanExpressionPtr;

auto input(const std::uint32_t column, const types::DataType data_type) -> PlanExpressionPtr {
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanInputColumnReference{.column = planner::PlanColumnIndex{column}}
    );
}

auto literal(types::Value value) -> PlanExpressionPtr {
    const auto data_type = value.data_type();
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanLiteralExpression{.value = std::move(value)}
    );
}

auto schema(std::vector<planner::PlanOutputColumn> columns) -> planner::PlanSchema {
    return planner::PlanSchema{.columns = std::move(columns)};
}

auto empty_context() -> base::Result<ExecContext> {
    auto namespace_result = planner::ParameterNamespace::from_external_slots({});
    if (!namespace_result) {
        return base::fail(std::move(namespace_result).error());
    }
    return ExecContext::create(namespace_result.value(), {});
}

auto sort_item(
    PlanExpressionPtr expression,
    const planner::PlanSortDirection direction = planner::PlanSortDirection::kDefault,
    const planner::PlanNullOrder null_order = planner::PlanNullOrder::kDefault
) -> planner::PlanSortItem {
    return planner::PlanSortItem{
        .expression = std::move(expression),
        .direction = direction,
        .null_order = null_order,
    };
}

auto make_values_plan() -> planner::ValuesPlan {
    const auto output = schema({
        planner::PlanOutputColumn{.name = "id", .data_type = types::kInt64Type},
        planner::PlanOutputColumn{.name = "key", .data_type = types::kInt64Type},
    });
    planner::ValuesPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .rows = {},
    };
    const std::vector<std::pair<std::int64_t, std::int64_t>> rows{
        {0, 2},
        {1, 1},
        {2, 2},
        {3, 1},
    };
    for (const auto& [id, key] : rows) {
        std::vector<PlanExpressionPtr> row;
        row.push_back(literal(types::Value::integer(id)));
        row.push_back(literal(types::Value::integer(key)));
        plan.rows.push_back(std::move(row));
    }
    return plan;
}

TEST(SortSupportTest, UsesDirectionDependentDefaultNullOrderAndExplicitOverrides) {
    std::vector<planner::PlanSortItem> ascending;
    ascending.push_back(sort_item(input(0U, types::kInt64Type)));
    auto asc_support = SortSupport::compile(ascending);
    ASSERT_TRUE(asc_support) << asc_support.error().to_string();

    const std::vector left_null{types::Value::null(types::kInt64Type)};
    const std::vector right_value{types::Value::integer(1)};
    EXPECT_EQ(asc_support->compare(left_null, right_value), std::strong_ordering::greater);

    std::vector<planner::PlanSortItem> descending;
    descending.push_back(
        sort_item(input(0U, types::kInt64Type), planner::PlanSortDirection::kDescending)
    );
    auto desc_support = SortSupport::compile(descending);
    ASSERT_TRUE(desc_support) << desc_support.error().to_string();
    EXPECT_EQ(desc_support->compare(left_null, right_value), std::strong_ordering::less);

    std::vector<planner::PlanSortItem> explicit_last;
    explicit_last.push_back(sort_item(
        input(0U, types::kInt64Type),
        planner::PlanSortDirection::kDescending,
        planner::PlanNullOrder::kLast
    ));
    auto explicit_support = SortSupport::compile(explicit_last);
    ASSERT_TRUE(explicit_support) << explicit_support.error().to_string();
    EXPECT_EQ(explicit_support->compare(left_null, right_value), std::strong_ordering::greater);
}

TEST(SortSupportTest, ComparesFloatWithTotalOrderIncludingSignedZeroAndNaN) {
    std::vector<planner::PlanSortItem> order_by;
    order_by.push_back(sort_item(input(0U, types::kFloat32Type)));
    auto support = SortSupport::compile(order_by);
    ASSERT_TRUE(support) << support.error().to_string();

    const auto negative_zero = std::vector{types::Value::floating(-0.0F)};
    const auto positive_zero = std::vector{types::Value::floating(0.0F)};
    EXPECT_EQ(support->compare(negative_zero, positive_zero), std::strong_ordering::less);

    const auto finite = std::vector{types::Value::floating(1.0F)};
    const auto nan =
        std::vector{types::Value::floating(std::bit_cast<float>(std::uint32_t{0x7fc0'0001U}))};
    EXPECT_EQ(support->compare(finite, nan), std::strong_ordering::less);
}

TEST(SortSupportTest, EvaluateIntoRetainsTransientKeyStorage) {
    std::vector<planner::PlanSortItem> order_by;
    order_by.push_back(sort_item(input(0U, types::kInt64Type)));
    order_by.push_back(sort_item(input(1U, types::kInt64Type)));
    auto support = SortSupport::compile(order_by);
    ASSERT_TRUE(support) << support.error().to_string();
    auto descriptor = TupleDescriptor::create({
        TupleColumn{.name = "first", .data_type = types::kInt64Type},
        TupleColumn{.name = "second", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(descriptor) << descriptor.error().to_string();
    auto first =
        Tuple::create(descriptor.value(), {types::Value::integer(1), types::Value::integer(2)});
    auto second =
        Tuple::create(descriptor.value(), {types::Value::integer(3), types::Value::integer(4)});
    auto context = empty_context();
    ASSERT_TRUE(first) << first.error().to_string();
    ASSERT_TRUE(second) << second.error().to_string();
    ASSERT_TRUE(context) << context.error().to_string();

    SortKeyValues keys;
    ASSERT_TRUE(support->evaluate_into(first.value(), context.value(), keys));
    ASSERT_EQ(keys.size(), 2U);
    const types::Value* const storage = keys.data();
    ASSERT_TRUE(support->evaluate_into(second.value(), context.value(), keys));
    EXPECT_EQ(keys.data(), storage);
    EXPECT_EQ(keys[0U], types::Value::integer(3));
    EXPECT_EQ(keys[1U], types::Value::integer(4));
}

TEST(SortSupportTest, DirectColumnKeyRejectsRuntimeShapeDrift) {
    std::vector<planner::PlanSortItem> order_by;
    order_by.push_back(sort_item(input(1U, types::kInt64Type)));
    auto support = SortSupport::compile(order_by);
    ASSERT_TRUE(support) << support.error().to_string();
    auto descriptor = TupleDescriptor::create({
        TupleColumn{.name = "only", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(descriptor) << descriptor.error().to_string();
    auto tuple = Tuple::create(descriptor.value(), {types::Value::integer(7)});
    auto context = empty_context();
    ASSERT_TRUE(tuple) << tuple.error().to_string();
    ASSERT_TRUE(context) << context.error().to_string();

    const auto evaluated = support->evaluate(tuple.value(), context.value());
    ASSERT_FALSE(evaluated);
    EXPECT_EQ(evaluated.error().code(), base::ErrorCode::kExecution);
}

TEST(SortSupportTest, MovingTransfersKeysAndMakesSourceInert) {
    std::vector<planner::PlanSortItem> ascending;
    ascending.push_back(sort_item(input(0U, types::kInt64Type)));
    auto first = SortSupport::compile(ascending);
    ASSERT_TRUE(first);

    auto descriptor = TupleDescriptor::create({
        TupleColumn{.name = "value", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(descriptor);
    auto tuple = Tuple::create(descriptor.value(), {types::Value::integer(7)});
    auto context = empty_context();
    ASSERT_TRUE(tuple);
    ASSERT_TRUE(context);

    SortSupport moved{std::move(first).value()};
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.key_count(), 1U);
    ASSERT_TRUE(moved.evaluate(tuple.value(), context.value()));
    // NOLINTNEXTLINE(bugprone-use-after-move) - verifies the documented inert source.
    EXPECT_FALSE(first->valid());
    // NOLINTNEXTLINE(bugprone-use-after-move) - moved-from evaluation must reject access.
    EXPECT_EQ(
        first->evaluate(tuple.value(), context.value()).error().code(),
        base::ErrorCode::kUnavailable
    );

    std::vector<planner::PlanSortItem> descending;
    descending.push_back(
        sort_item(input(0U, types::kInt64Type), planner::PlanSortDirection::kDescending)
    );
    auto second = SortSupport::compile(descending);
    ASSERT_TRUE(second);
    moved = std::move(second).value();
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.key_count(), 1U);
    // NOLINTNEXTLINE(bugprone-use-after-move) - verifies move-assignment source cleanup.
    EXPECT_FALSE(second->valid());
}

TEST(SortStateTest, MaterializesOncePreservesStableTiesAndRescans) {
    auto values_plan = make_values_plan();
    auto child = ValuesState::create(values_plan);
    ASSERT_TRUE(child) << child.error().to_string();

    const auto output = values_plan.metadata.output;
    planner::SortPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .order_by = {},
    };
    plan.order_by.push_back(sort_item(input(1U, types::kInt64Type)));
    auto state = SortState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));

    const std::vector<std::int64_t> expected_ids{1, 3, 0, 2};
    for (const auto expected_id : expected_ids) {
        ASSERT_TRUE(state.value()->next(context.value()));
        const auto* tuple = state.value()->output_slot().tuple();
        ASSERT_NE(tuple, nullptr);
        ASSERT_EQ(tuple->value_at(0U).value().get(), types::Value::integer(expected_id));
    }
    auto end = state.value()->next(context.value());
    ASSERT_TRUE(end);
    EXPECT_FALSE(end.value());

    ASSERT_TRUE(state.value()->rescan(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));
    EXPECT_EQ(
        state.value()->output_slot().tuple()->value_at(0U).value().get(),
        types::Value::integer(1)
    );
}

TEST(SortStateTest, RejectsAnEmptyOrderByList) {
    auto child = ValuesState::create(make_values_plan());
    ASSERT_TRUE(child);
    const auto output = make_values_plan().metadata.output;
    planner::SortPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .order_by = {},
    };
    auto state = SortState::create(plan, std::move(child).value());
    ASSERT_FALSE(state);
    EXPECT_EQ(state.error().code(), base::ErrorCode::kPlan);
}

}  // namespace
}  // namespace db::execution
