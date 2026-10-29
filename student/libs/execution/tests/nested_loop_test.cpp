#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/execution/nested_loop.hpp"
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

auto literal(const std::int64_t value) -> PlanExpressionPtr {
    return literal_value(types::Value::integer(value));
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

auto one_column_schema(const char* name) -> planner::PlanSchema {
    return planner::PlanSchema{
        .columns = {{.name = name, .data_type = types::kInt64Type}},
    };
}

auto joined_schema(const planner::PlanSchema& outer, const planner::PlanSchema& inner)
    -> planner::PlanSchema {
    planner::PlanSchema result{.columns = outer.columns};
    result.columns.insert(result.columns.end(), inner.columns.begin(), inner.columns.end());
    return result;
}

auto values_plan(const planner::PlanSchema& schema, const std::vector<std::int64_t>& values)
    -> planner::ValuesPlan {
    planner::ValuesPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .rows = {},
    };
    for (const std::int64_t value : values) {
        std::vector<PlanExpressionPtr> row;
        row.push_back(literal(value));
        plan.rows.push_back(std::move(row));
    }
    return plan;
}

auto empty_context(planner::PlanParameterReference* const reference = nullptr)
    -> base::Result<ExecContext> {
    auto namespace_result = planner::ParameterNamespace::from_external_slots({});
    if (!namespace_result) {
        return base::fail(std::move(namespace_result).error());
    }
    if (reference != nullptr) {
        auto allocated = namespace_result->allocate_executor_parameter(types::kInt64Type);
        if (!allocated) {
            return base::fail(std::move(allocated).error());
        }
        *reference = allocated.value();
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
                    "nested-loop test state produced no output tuple",
                }
            );
        }
        rows.emplace_back(tuple->values().begin(), tuple->values().end());
    }
}

class FilteringIndexPort final : public IndexScanPort {
public:
    explicit FilteringIndexPort(std::vector<Tuple> rows)
        : rows_{std::move(rows)} {}

    auto configure(IndexScanRequest request) -> base::Status override {
        request_ = std::move(request);
        configured_ = true;
        return {};
    }

    auto bind(std::span<const IndexScanBound> bounds) -> base::Status override {
        bounds_.assign(bounds.begin(), bounds.end());
        cursor_ = 0U;
        ++bind_count_;
        return {};
    }

    auto open(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        if (!configured_) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kExecution,
                    "index port was not configured",
                }
            );
        }
        cursor_ = 0U;
        ++open_count_;
        return {};
    }

    auto next(ExecContext& context, std::vector<types::Value> recycled_values)
        -> base::Result<std::optional<Tuple>> override {
        static_cast<void>(context);
        static_cast<void>(recycled_values);
        while (cursor_ < rows_.size()) {
            Tuple row = rows_[cursor_++];
            if (bounds_.empty() || row.values().front() == bounds_.front().value) {
                return std::optional<Tuple>{std::move(row)};
            }
        }
        return std::optional<Tuple>{};
    }

    auto rescan(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        cursor_ = 0U;
        ++rescan_count_;
        return {};
    }

    auto close(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        ++close_count_;
        return {};
    }

    auto bind_count() const noexcept -> std::size_t {
        return bind_count_;
    }

    auto open_count() const noexcept -> std::size_t {
        return open_count_;
    }

    auto rescan_count() const noexcept -> std::size_t {
        return rescan_count_;
    }

private:
    std::vector<Tuple> rows_;
    IndexScanRequest request_;
    std::vector<IndexScanBound> bounds_;
    std::size_t cursor_{0U};
    std::size_t bind_count_{0U};
    std::size_t open_count_{0U};
    std::size_t rescan_count_{0U};
    std::size_t close_count_{0U};
    bool configured_{false};
};

auto integer_descriptor(const char* name) -> base::Result<TupleDescriptorHandle> {
    return TupleDescriptor::create({TupleColumn{.name = name, .data_type = types::kInt64Type}});
}

auto integer_tuple(const char* name, const std::int64_t value) -> base::Result<Tuple> {
    auto descriptor = integer_descriptor(name);
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    return Tuple::create(std::move(descriptor).value(), {types::Value::integer(value)});
}

auto index_state(
    const planner::PlanSchema& schema,
    std::unique_ptr<IndexScanPort> port,
    const planner::PlanParameterReference& parameter
) -> base::Result<std::unique_ptr<IndexScanState>> {
    planner::IndexScanPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{11U},
                .name = "inner",
                .schema = schema,
            },
        .index =
            planner::PlanIndex{
                .oid = base::Oid{12U},
                .name = "inner_id_idx",
                .keys = {{
                    .relation_column = planner::PlanColumnIndex{0U},
                    .column_name = "id",
                    .data_type = types::kInt64Type,
                }},
                .unique = false,
            },
        .bounds = {},
        .required_columns = std::nullopt,
        .filter = nullptr,
    };
    plan.bounds.push_back(
        planner::PlanIndexBound{
            .key_index = planner::PlanIndexKeyIndex{0U},
            .comparison = types::BinaryOperator::kEqual,
            .value = std::make_unique<PlanExpression>(
                planner::PlanSourceSpan{},
                types::kInt64Type,
                parameter
            ),
        }
    );
    return IndexScanState::create(plan, std::move(port));
}

TEST(NestedLoopStateTest, EmitsCrossProductInStableOuterThenInnerOrder) {
    const auto outer_schema = one_column_schema("outer_id");
    const auto inner_schema = one_column_schema("inner_id");
    auto outer = ValuesState::create(values_plan(outer_schema, {1, 2}));
    auto inner = ValuesState::create(values_plan(inner_schema, {10, 20}));
    ASSERT_TRUE(outer) << outer.error().to_string();
    ASSERT_TRUE(inner) << inner.error().to_string();
    planner::NestedLoopPlan plan{
        .metadata =
            planner::PlanOperatorMetadata{.output = joined_schema(outer_schema, inner_schema)},
        .outer = nullptr,
        .inner = nullptr,
        .join_filter = nullptr,
        .parameters = {},
    };
    auto state = NestedLoopState::create(plan, std::move(outer).value(), std::move(inner).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows) << rows.error().to_string();
    ASSERT_EQ(rows->size(), 4U);
    EXPECT_EQ(
        rows->at(0U),
        (std::vector<types::Value>{types::Value::integer(1), types::Value::integer(10)})
    );
    EXPECT_EQ(
        rows->at(1U),
        (std::vector<types::Value>{types::Value::integer(1), types::Value::integer(20)})
    );
    EXPECT_EQ(
        rows->at(2U),
        (std::vector<types::Value>{types::Value::integer(2), types::Value::integer(10)})
    );
    EXPECT_EQ(
        rows->at(3U),
        (std::vector<types::Value>{types::Value::integer(2), types::Value::integer(20)})
    );
}

TEST(NestedLoopStateTest, AppliesJoinQualificationOnlyWhenTrue) {
    const auto outer_schema = one_column_schema("outer_id");
    const auto inner_schema = one_column_schema("inner_id");
    auto outer = ValuesState::create(values_plan(outer_schema, {1, 2}));
    auto inner_plan = values_plan(inner_schema, {2, 3});
    std::vector<PlanExpressionPtr> null_row;
    null_row.push_back(literal_value(types::Value::null(types::kInt64Type)));
    inner_plan.rows.push_back(std::move(null_row));
    auto inner = ValuesState::create(inner_plan);
    ASSERT_TRUE(outer);
    ASSERT_TRUE(inner);
    planner::NestedLoopPlan plan{
        .metadata =
            planner::PlanOperatorMetadata{.output = joined_schema(outer_schema, inner_schema)},
        .outer = nullptr,
        .inner = nullptr,
        .join_filter = equal(input(0U), input(1U)),
        .parameters = {},
    };
    auto state = NestedLoopState::create(plan, std::move(outer).value(), std::move(inner).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows);
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(
        rows->front(),
        (std::vector<types::Value>{types::Value::integer(2), types::Value::integer(2)})
    );
    ASSERT_TRUE(state.value()->close(context.value()));
    EXPECT_EQ(state.value()->lifecycle(), PlanStateLifecycle::kClosed);
}

TEST(NestedLoopStateTest, RebindsParameterizedIndexScanForEveryOuterTupleAndRescan) {
    const auto outer_schema = one_column_schema("outer_id");
    const auto inner_schema = one_column_schema("inner_id");
    auto outer = ValuesState::create(values_plan(outer_schema, {1, 2}));
    ASSERT_TRUE(outer);
    std::vector<Tuple> rows;
    rows.push_back(integer_tuple("inner_id", 1).value());
    rows.push_back(integer_tuple("inner_id", 1).value());
    rows.push_back(integer_tuple("inner_id", 2).value());
    rows.push_back(integer_tuple("inner_id", 3).value());
    auto port = std::make_unique<FilteringIndexPort>(std::move(rows));
    auto* const port_view = port.get();
    planner::PlanParameterReference parameter;
    auto context = empty_context(&parameter);
    ASSERT_TRUE(context) << context.error().to_string();
    auto inner = index_state(inner_schema, std::move(port), parameter);
    ASSERT_TRUE(inner) << inner.error().to_string();
    planner::NestedLoopPlan plan{
        .metadata =
            planner::PlanOperatorMetadata{.output = joined_schema(outer_schema, inner_schema)},
        .outer = nullptr,
        .inner = nullptr,
        .join_filter = nullptr,
        .parameters = {{
            .parameter = parameter,
            .outer_column = planner::PlanColumnIndex{0U},
        }},
    };
    auto state = NestedLoopState::create(plan, std::move(outer).value(), std::move(inner).value());
    ASSERT_TRUE(state) << state.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows_first = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows_first) << rows_first.error().to_string();
    ASSERT_EQ(rows_first->size(), 3U);
    EXPECT_EQ(
        rows_first->at(0U),
        (std::vector<types::Value>{types::Value::integer(1), types::Value::integer(1)})
    );
    EXPECT_EQ(
        rows_first->at(1U),
        (std::vector<types::Value>{types::Value::integer(1), types::Value::integer(1)})
    );
    EXPECT_EQ(
        rows_first->at(2U),
        (std::vector<types::Value>{types::Value::integer(2), types::Value::integer(2)})
    );
    EXPECT_EQ(port_view->open_count(), 1U);
    EXPECT_EQ(port_view->bind_count(), 2U);
    EXPECT_EQ(port_view->rescan_count(), 1U);

    ASSERT_TRUE(state.value()->rescan(context.value()));
    auto rows_second = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows_second);
    EXPECT_EQ(rows_second.value(), rows_first.value());
    EXPECT_EQ(port_view->bind_count(), 4U);
    EXPECT_EQ(port_view->rescan_count(), 3U);
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(NestedLoopStateTest, DoesNotOpenInnerWhenOuterIsEmpty) {
    const auto outer_schema = one_column_schema("outer_id");
    const auto inner_schema = one_column_schema("inner_id");
    auto outer = ValuesState::create(values_plan(outer_schema, {}));
    ASSERT_TRUE(outer);
    auto port = std::make_unique<FilteringIndexPort>(std::vector<Tuple>{});
    auto* const port_view = port.get();
    planner::PlanParameterReference parameter;
    auto context = empty_context(&parameter);
    ASSERT_TRUE(context);
    auto inner = index_state(inner_schema, std::move(port), parameter);
    ASSERT_TRUE(inner);
    planner::NestedLoopPlan plan{
        .metadata =
            planner::PlanOperatorMetadata{.output = joined_schema(outer_schema, inner_schema)},
        .outer = nullptr,
        .inner = nullptr,
        .join_filter = nullptr,
        .parameters = {{.parameter = parameter, .outer_column = planner::PlanColumnIndex{0U}}},
    };
    auto state = NestedLoopState::create(plan, std::move(outer).value(), std::move(inner).value());
    ASSERT_TRUE(state);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto next = state.value()->next(context.value());
    ASSERT_TRUE(next);
    EXPECT_FALSE(next.value());
    EXPECT_EQ(port_view->open_count(), 0U);
}

}  // namespace
}  // namespace db::execution
