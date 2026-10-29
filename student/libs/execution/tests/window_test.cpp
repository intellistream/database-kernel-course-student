#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/execution/limit.hpp"
#include "db/execution/operators.hpp"
#include "db/execution/row_count.hpp"
#include "db/execution/topn.hpp"
#include "db/execution/unique.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::execution {
namespace {

using planner::PlanExpressionPtr;

auto literal(types::Value value) -> PlanExpressionPtr {
    const auto data_type = value.data_type();
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanLiteralExpression{.value = std::move(value)}
    );
}

auto executor_parameter(const planner::PlanParameterId id, const types::DataType data_type)
    -> PlanExpressionPtr {
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanParameterReference{
            .id = id,
            .kind = planner::PlanParameterKind::kExecutor,
            .data_type = data_type,
        }
    );
}

auto integer_schema() -> planner::PlanSchema {
    return planner::PlanSchema{
        .columns = {
            planner::PlanOutputColumn{.name = "value", .data_type = types::kInt64Type},
        }
    };
}

auto keyed_schema() -> planner::PlanSchema {
    return planner::PlanSchema{
        .columns = {
            planner::PlanOutputColumn{.name = "id", .data_type = types::kInt64Type},
            planner::PlanOutputColumn{.name = "key", .data_type = types::kInt64Type},
        }
    };
}

auto values_plan(const std::vector<std::int64_t>& values) -> planner::ValuesPlan {
    planner::ValuesPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = integer_schema()},
        .rows = {},
    };
    for (const std::int64_t value : values) {
        std::vector<PlanExpressionPtr> row;
        row.push_back(literal(types::Value::integer(value)));
        plan.rows.push_back(std::move(row));
    }
    return plan;
}

auto keyed_values_plan(const std::vector<std::pair<std::int64_t, std::int64_t>>& values)
    -> planner::ValuesPlan {
    planner::ValuesPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = keyed_schema()},
        .rows = {},
    };
    for (const auto& [id, key] : values) {
        std::vector<PlanExpressionPtr> row;
        row.push_back(literal(types::Value::integer(id)));
        row.push_back(literal(types::Value::integer(key)));
        plan.rows.push_back(std::move(row));
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

auto executor_context(const types::Value& value, planner::PlanParameterReference* const reference)
    -> base::Result<ExecContext> {
    auto parameters = planner::ParameterNamespace::from_external_slots({});
    if (!parameters) {
        return base::fail(std::move(parameters).error());
    }
    auto allocated = parameters->allocate_executor_parameter(value.data_type());
    if (!allocated) {
        return base::fail(std::move(allocated).error());
    }
    *reference = allocated.value();
    auto context = ExecContext::create(parameters.value(), {});
    if (!context) {
        return base::fail(std::move(context).error());
    }
    auto bound = context->set_executor_parameter(*reference, value);
    if (!bound) {
        return base::fail(std::move(bound).error());
    }
    return std::move(context).value();
}

auto input_column(const std::uint32_t column = 0U) -> planner::PlanExpressionPtr {
    return std::make_unique<planner::PlanExpression>(
        planner::PlanSourceSpan{},
        types::kInt64Type,
        planner::PlanInputColumnReference{.column = planner::PlanColumnIndex{column}}
    );
}

auto read_rows(PlanState& state, ExecContext& context) -> std::vector<std::int64_t> {
    std::vector<std::int64_t> result;
    while (true) {
        auto next = state.next(context);
        EXPECT_TRUE(next) << next.error().to_string();
        if (!next || !next.value()) {
            break;
        }
        const Tuple* const tuple = state.output_slot().tuple();
        if (tuple == nullptr) {
            ADD_FAILURE() << "state reported a row without output";
            break;
        }
        const auto value = tuple->value_at(0U);
        if (!value || value->get().as_integer() == nullptr) {
            ADD_FAILURE() << "state produced a non-integer output";
            break;
        }
        result.push_back(*value->get().as_integer());
    }
    return result;
}

void expect_inactive_row_count(RowCountSupport& support, const ExecContext& context) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(support.valid());
    EXPECT_FALSE(support.has_limit());
    const auto evaluated = support.evaluate(context);
    ASSERT_FALSE(evaluated);
    EXPECT_EQ(evaluated.error().code(), base::ErrorCode::kUnavailable);
}

TEST(WindowStateTest, MovingRowCountSupportTransfersProgramsAndMakesSourceInert) {
    auto context = empty_context();
    ASSERT_TRUE(context);
    auto without_window = RowCountSupport::compile(nullptr, nullptr);
    ASSERT_TRUE(without_window);

    RowCountSupport moved{std::move(without_window).value()};
    EXPECT_TRUE(moved.valid());
    EXPECT_FALSE(moved.has_limit());
    auto evaluated = moved.evaluate(context.value());
    ASSERT_TRUE(evaluated);
    EXPECT_FALSE(evaluated->limit.has_value());
    EXPECT_EQ(evaluated->offset, 0U);
    // NOLINTNEXTLINE(bugprone-use-after-move) - verifies the documented inert source.
    expect_inactive_row_count(without_window.value(), context.value());

    auto limit = literal(types::Value::integer(3));
    auto with_limit = RowCountSupport::compile(limit.get(), nullptr);
    ASSERT_TRUE(with_limit);
    moved = std::move(with_limit).value();
    EXPECT_TRUE(moved.valid());
    EXPECT_TRUE(moved.has_limit());
    evaluated = moved.evaluate(context.value());
    ASSERT_TRUE(evaluated);
    ASSERT_TRUE(evaluated->limit.has_value());
    EXPECT_EQ(*evaluated->limit, 3U);
    // NOLINTNEXTLINE(bugprone-use-after-move) - verifies the documented inert source.
    expect_inactive_row_count(with_limit.value(), context.value());
}

TEST(WindowStateTest, LimitSkipsAndEmitsStreaminglyThenRescans) {
    auto child = ValuesState::create(values_plan({0, 1, 2, 3, 4}));
    ASSERT_TRUE(child) << child.error().to_string();
    const auto schema = integer_schema();
    planner::LimitPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .input = nullptr,
        .limit = literal(types::Value::integer(2)),
        .offset = literal(types::Value::integer(1)),
    };
    auto state = LimitState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    EXPECT_EQ(read_rows(*state.value(), context.value()), (std::vector<std::int64_t>{1, 2}));
    ASSERT_TRUE(state.value()->rescan(context.value()));
    EXPECT_EQ(read_rows(*state.value(), context.value()), (std::vector<std::int64_t>{1, 2}));
}

TEST(WindowStateTest, TopNUsesBoundedWindowAndStableTieOrder) {
    auto child = ValuesState::create(keyed_values_plan({{0, 1}, {1, 1}, {2, 0}, {3, 1}}));
    ASSERT_TRUE(child) << child.error().to_string();
    const auto schema = keyed_schema();
    planner::TopNPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .input = nullptr,
        .order_by = {},
        .limit = literal(types::Value::integer(3)),
        .offset = literal(types::Value::integer(0)),
    };
    plan.order_by.push_back(planner::PlanSortItem{.expression = input_column(1U)});
    auto state = TopNState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    EXPECT_EQ(read_rows(*state.value(), context.value()), (std::vector<std::int64_t>{2, 0, 1}));
}

TEST(WindowStateTest, TopNMatchesStableSortAcrossHeapShapesAndRescans) {
    std::vector<std::pair<std::int64_t, std::int64_t>> rows;
    rows.reserve(97U);
    for (std::int64_t id = 0; id < 97; ++id) {
        rows.emplace_back(id, (id * 37) % 17);
    }
    for (const bool descending : {false, true}) {
        auto sorted = rows;
        std::stable_sort(
            sorted.begin(),
            sorted.end(),
            [descending](const auto& left, const auto& right) {
                return descending ? left.second > right.second : left.second < right.second;
            }
        );
        for (const std::int64_t limit : {1, 2, 3, 4, 7, 32, 128}) {
            for (const std::int64_t offset : {0, 2}) {
                SCOPED_TRACE(::testing::Message() << descending << ',' << limit << ',' << offset);
                auto child = ValuesState::create(keyed_values_plan(rows));
                ASSERT_TRUE(child);
                planner::TopNPlan plan{
                    .metadata = planner::PlanOperatorMetadata{.output = keyed_schema()},
                    .input = nullptr,
                    .order_by = {},
                    .limit = literal(types::Value::integer(limit)),
                    .offset = literal(types::Value::integer(offset)),
                };
                plan.order_by.push_back(
                    planner::PlanSortItem{
                        .expression = input_column(1U),
                        .direction = descending ? planner::PlanSortDirection::kDescending
                                                : planner::PlanSortDirection::kAscending,
                    }
                );
                auto state = TopNState::create(plan, std::move(child).value());
                ASSERT_TRUE(state);
                auto context = empty_context();
                ASSERT_TRUE(context);
                std::vector<std::int64_t> expected;
                const auto end = std::min(sorted.size(), static_cast<std::size_t>(limit + offset));
                for (auto index = static_cast<std::size_t>(offset); index < end; ++index) {
                    expected.push_back(sorted[index].first);
                }
                ASSERT_TRUE(state.value()->open(context.value()));
                EXPECT_EQ(read_rows(*state.value(), context.value()), expected);
                ASSERT_TRUE(state.value()->rescan(context.value()));
                EXPECT_EQ(read_rows(*state.value(), context.value()), expected);
                ASSERT_TRUE(state.value()->close(context.value()));
            }
        }
    }
}

TEST(WindowStateTest, TopNZeroLimitDoesNotReadChild) {
    auto child = ValuesState::create(values_plan({2, 1, 0}));
    ASSERT_TRUE(child) << child.error().to_string();
    planner::TopNPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = integer_schema()},
        .input = nullptr,
        .order_by = {},
        .limit = literal(types::Value::integer(0)),
        .offset = literal(types::Value::integer(0)),
    };
    plan.order_by.push_back(planner::PlanSortItem{.expression = input_column()});
    auto state = TopNState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    auto next = state.value()->next(context.value());
    ASSERT_TRUE(next) << next.error().to_string();
    EXPECT_FALSE(next.value());
}

TEST(WindowStateTest, UniqueEmitsFirstCompleteValueVectorAndRescans) {
    auto child = ValuesState::create(values_plan({2, 1, 2, 1, 3}));
    ASSERT_TRUE(child) << child.error().to_string();
    planner::UniquePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = integer_schema()},
        .input = nullptr,
    };
    auto state = UniqueState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    EXPECT_EQ(read_rows(*state.value(), context.value()), (std::vector<std::int64_t>{2, 1, 3}));
    ASSERT_TRUE(state.value()->rescan(context.value()));
    EXPECT_EQ(read_rows(*state.value(), context.value()), (std::vector<std::int64_t>{2, 1, 3}));
}

TEST(WindowStateTest, RebindsExecutorLimitOnRescanAndRejectsNegativeCounts) {
    planner::PlanParameterReference reference{};
    auto context = executor_context(types::Value::integer(1), &reference);
    ASSERT_TRUE(context) << context.error().to_string();
    auto child = ValuesState::create(values_plan({0, 1, 2}));
    ASSERT_TRUE(child) << child.error().to_string();
    planner::LimitPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = integer_schema()},
        .input = nullptr,
        .limit = executor_parameter(reference.id, types::kInt64Type),
        .offset = nullptr,
    };
    auto state = LimitState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    EXPECT_EQ(read_rows(*state.value(), context.value()), (std::vector<std::int64_t>{0}));
    ASSERT_TRUE(context->set_executor_parameter(reference, types::Value::integer(2)));
    ASSERT_TRUE(state.value()->rescan(context.value()));
    EXPECT_EQ(read_rows(*state.value(), context.value()), (std::vector<std::int64_t>{0, 1}));

    auto negative_child = ValuesState::create(values_plan({0}));
    ASSERT_TRUE(negative_child);
    planner::LimitPlan negative_plan{
        .metadata = planner::PlanOperatorMetadata{.output = integer_schema()},
        .input = nullptr,
        .limit = literal(types::Value::integer(-1)),
        .offset = nullptr,
    };
    auto negative = LimitState::create(negative_plan, std::move(negative_child).value());
    ASSERT_TRUE(negative);
    auto negative_open = negative.value()->open(context.value());
    ASSERT_FALSE(negative_open);
    EXPECT_EQ(negative_open.error().code(), base::ErrorCode::kOutOfRange);
}

TEST(WindowStateTest, RejectsRowDependentWindowExpressionsAtConstruction) {
    auto child = ValuesState::create(values_plan({0, 1}));
    ASSERT_TRUE(child) << child.error().to_string();
    planner::LimitPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = integer_schema()},
        .input = nullptr,
        .limit = input_column(),
        .offset = nullptr,
    };
    auto state = LimitState::create(plan, std::move(child).value());
    ASSERT_FALSE(state);
    EXPECT_EQ(state.error().code(), base::ErrorCode::kPlan);
}

}  // namespace
}  // namespace db::execution
