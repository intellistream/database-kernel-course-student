#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/execution/modify_table.hpp"
#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"
#include "db/types/value.hpp"

namespace db::execution {
namespace {

using planner::PlanExpression;
using planner::PlanExpressionPtr;

auto literal(types::Value value) -> PlanExpressionPtr {
    const types::DataType data_type = value.data_type();
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanLiteralExpression{.value = std::move(value)}
    );
}

auto input(const std::uint32_t column, const types::DataType type) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        type,
        planner::PlanInputColumnReference{.column = planner::PlanColumnIndex{column}}
    );
}

auto binary(
    const types::BinaryOperator operation,
    const types::DataType type,
    PlanExpressionPtr left,
    PlanExpressionPtr right
) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        type,
        planner::PlanBinaryExpression{
            .op = operation,
            .left = std::move(left),
            .right = std::move(right),
        }
    );
}

auto target_schema() -> planner::PlanSchema {
    return planner::PlanSchema{
        .columns = {
            planner::PlanOutputColumn{.name = "id", .data_type = types::kInt64Type},
            planner::PlanOutputColumn{.name = "value", .data_type = types::kInt64Type},
        },
    };
}

auto target_relation() -> planner::PlanRelation {
    return planner::PlanRelation{
        .oid = base::Oid{42U},
        .name = "items",
        .alias = std::nullopt,
        .schema = target_schema(),
    };
}

auto empty_context() -> base::Result<ExecContext> {
    auto namespace_result = planner::ParameterNamespace::from_external_slots({});
    if (!namespace_result) {
        return base::fail(std::move(namespace_result).error());
    }
    return ExecContext::create(namespace_result.value(), {});
}

auto descriptor_for(const planner::PlanSchema& schema) -> base::Result<TupleDescriptorHandle> {
    return TupleDescriptor::from_plan_schema(schema);
}

class RidSourceState final : public PlanState {
public:
    RidSourceState(
        TupleSlot slot,
        std::vector<std::vector<types::Value>> rows,
        std::vector<std::optional<base::Rid>> rids
    )
        : PlanState{std::move(slot)},
          rows_{std::move(rows)},
          rids_{std::move(rids)} {}

protected:
    auto do_open(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        position_ = 0U;
        return {};
    }

    auto do_next(ExecContext& context) -> base::Result<bool> override {
        static_cast<void>(context);
        if (position_ >= rows_.size()) {
            return false;
        }
        store_output_values(rows_[position_], rids_[position_]);
        ++position_;
        return true;
    }

    auto do_rescan(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        position_ = 0U;
        return {};
    }

    auto do_close(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        return {};
    }

private:
    std::vector<std::vector<types::Value>> rows_;
    std::vector<std::optional<base::Rid>> rids_;
    std::size_t position_{0U};
};

class TestFactory final : public PlanStateFactory {
public:
    TestFactory(
        planner::PlanSchema schema,
        std::vector<std::vector<types::Value>> rows,
        std::vector<std::optional<base::Rid>> rids
    )
        : schema_{std::move(schema)},
          rows_{std::move(rows)},
          rids_{std::move(rids)} {}

    auto create(const planner::Plan& plan) -> base::Result<std::unique_ptr<PlanState>> override {
        static_cast<void>(plan);
        auto descriptor = descriptor_for(schema_);
        if (!descriptor) {
            return base::fail(std::move(descriptor).error());
        }
        auto slot = TupleSlot::create(std::move(descriptor).value());
        if (!slot) {
            return base::fail(std::move(slot).error());
        }
        return std::unique_ptr<PlanState>{new RidSourceState{
            std::move(slot).value(),
            rows_,
            rids_,
        }};
    }

private:
    planner::PlanSchema schema_;
    std::vector<std::vector<types::Value>> rows_;
    std::vector<std::optional<base::Rid>> rids_;
};

class RecordingPort final : public ModifyTablePort {
public:
    struct UpdateCall final {
        base::Rid rid{base::kInvalidRid};
        std::vector<types::Value> old_values;
        std::vector<types::Value> new_values;
        bool indexed_columns_unchanged{false};
    };

    auto insert(const ModifyTarget& target, std::span<const types::Value> values)
        -> base::Result<base::Rid> override {
        targets.push_back(target);
        inserts.emplace_back(values.begin(), values.end());
        return base::Rid{
            base::PageId{10U},
            base::SlotId{static_cast<std::uint16_t>(inserts.size())}
        };
    }

    auto update(
        const ModifyTarget& target,
        const base::Rid rid,
        std::span<const types::Value> old_values,
        std::span<const types::Value> new_values
    ) -> base::Result<ModifyUpdateResult> override {
        const bool values_unchanged =
            std::equal(old_values.begin(), old_values.end(), new_values.begin());
        targets.push_back(target);
        updates.push_back(
            UpdateCall{
                .rid = rid,
                .old_values = {old_values.begin(), old_values.end()},
                .new_values = {new_values.begin(), new_values.end()},
                .indexed_columns_unchanged = values_unchanged,
            }
        );
        return ModifyUpdateResult{
            .rid =
                base::Rid{
                    base::PageId{11U},
                    base::SlotId{static_cast<std::uint16_t>(updates.size())}
                },
            .index_maintenance_required = !values_unchanged,
        };
    }

    auto delete_row(
        const ModifyTarget& target,
        const base::Rid rid,
        std::span<const types::Value> old_values
    ) -> base::Status override {
        targets.push_back(target);
        deletes.push_back(
            UpdateCall{
                .rid = rid,
                .old_values = {old_values.begin(), old_values.end()},
                .new_values = {},
                .indexed_columns_unchanged = false,
            }
        );
        return {};
    }

    std::vector<ModifyTarget> targets;
    std::vector<std::vector<types::Value>> inserts;
    std::vector<UpdateCall> updates;
    std::vector<UpdateCall> deletes;
};

auto source_plan(const planner::PlanSchema& schema) -> planner::Plan {
    return planner::Plan{planner::ValuesPlan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .rows = {},
    }};
}

auto query_insert_plan(
    const planner::PlanSchema& schema,
    std::vector<planner::PlanColumnIndex> target_columns
) -> planner::ModifyTablePlan {
    return planner::ModifyTablePlan{
        .metadata = planner::PlanOperatorMetadata{.output = {}},
        .operation = planner::ModifyOperation::kInsert,
        .target = target_relation(),
        .target_columns = std::move(target_columns),
        .assignments = {},
        .source = planner::PlanQueryModificationSource{
            .input = std::make_unique<planner::Plan>(source_plan(schema)),
        },
    };
}

auto source_rows() -> std::vector<std::vector<types::Value>> {
    return {
        {types::Value::integer(7), types::Value::integer(10)},
        {types::Value::integer(7), types::Value::integer(10)},
    };
}

auto duplicate_rids() -> std::vector<std::optional<base::Rid>> {
    const base::Rid rid{base::PageId{3U}, base::SlotId{4U}};
    return {rid, rid};
}

TEST(ModifyTableStateTest, InsertsIdentityMappedQueryRows) {
    const auto schema = target_schema();
    const auto plan =
        query_insert_plan(schema, {planner::PlanColumnIndex{0U}, planner::PlanColumnIndex{1U}});
    RecordingPort port;
    TestFactory factory{schema, source_rows(), {std::nullopt, std::nullopt}};
    auto state = ModifyTableState::create(plan, factory, port);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto first = state.value()->next(context.value());
    ASSERT_TRUE(first);
    EXPECT_TRUE(first.value());
    auto second = state.value()->next(context.value());
    ASSERT_TRUE(second);
    EXPECT_TRUE(second.value());
    auto exhausted = state.value()->next(context.value());
    ASSERT_TRUE(exhausted);
    EXPECT_FALSE(exhausted.value());
    ASSERT_EQ(port.inserts.size(), 2U);
    EXPECT_EQ(port.inserts[0], source_rows()[0]);
    EXPECT_EQ(port.inserts[1], source_rows()[1]);
    EXPECT_EQ(state.value()->affected_rows(), 2U);
}

TEST(ModifyTableStateTest, MaterializesReorderedQueryInsertMapping) {
    const auto schema = target_schema();
    const auto plan =
        query_insert_plan(schema, {planner::PlanColumnIndex{1U}, planner::PlanColumnIndex{0U}});
    RecordingPort port;
    TestFactory factory{schema, source_rows(), {std::nullopt, std::nullopt}};
    auto state = ModifyTableState::create(plan, factory, port);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto first = state.value()->next(context.value());
    ASSERT_TRUE(first);
    EXPECT_TRUE(first.value());
    ASSERT_EQ(port.inserts.size(), 1U);
    EXPECT_EQ(
        port.inserts[0],
        (std::vector<types::Value>{types::Value::integer(10), types::Value::integer(7)})
    );
}

TEST(ModifyTableStateTest, InsertsMappedValuesAndRescans) {
    const auto schema = target_schema();
    std::vector<PlanExpressionPtr> insert_row;
    insert_row.push_back(literal(types::Value::integer(7)));
    planner::ModifyTablePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = {}},
        .operation = planner::ModifyOperation::kInsert,
        .target = target_relation(),
        .target_columns = {planner::PlanColumnIndex{1U}},
        .assignments = {},
        .source = planner::PlanValuesModificationSource{
            .rows = {},
        },
    };
    std::get<planner::PlanValuesModificationSource>(plan.source)
        .rows.push_back(std::move(insert_row));
    RecordingPort port;
    TestFactory factory{schema, {}, {}};
    auto state = ModifyTableState::create(plan, factory, port);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto first = state.value()->next(context.value());
    ASSERT_TRUE(first);
    ASSERT_TRUE(first.value());
    ASSERT_EQ(state.value()->output_slot().tuple()->values().size(), 0U);
    ASSERT_EQ(port.targets.size(), 1U);
    EXPECT_EQ(port.targets.front().relation_oid, base::Oid{42U});
    ASSERT_NE(port.targets.front().descriptor, nullptr);
    ASSERT_EQ(port.targets.front().descriptor->size(), 2U);
    EXPECT_EQ(port.targets.front().descriptor->columns()[0].name, "id");
    EXPECT_EQ(port.targets.front().descriptor->columns()[0].data_type, types::kInt64Type);
    EXPECT_EQ(port.targets.front().descriptor->columns()[1].name, "value");
    ASSERT_EQ(port.inserts.size(), 1U);
    EXPECT_EQ(
        port.inserts.front(),
        (std::vector<types::Value>{types::Value::null(types::kInt64Type), types::Value::integer(7)})
    );
    EXPECT_EQ(state.value()->affected_rows(), 1U);
    auto exhausted = state.value()->next(context.value());
    ASSERT_TRUE(exhausted);
    EXPECT_FALSE(exhausted.value());
    ASSERT_TRUE(state.value()->rescan(context.value()));
    auto repeated = state.value()->next(context.value());
    ASSERT_TRUE(repeated);
    EXPECT_TRUE(repeated.value());
    EXPECT_EQ(port.inserts.size(), 2U);
    ASSERT_EQ(port.targets.size(), 2U);
    EXPECT_EQ(port.targets[0].descriptor, port.targets[1].descriptor);
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(ModifyTableStateTest, UpdatesEachRidOnceAndProvidesOldAndNewRows) {
    const auto schema = target_schema();
    std::vector<planner::PlanAssignment> assignments;
    assignments.push_back(
        planner::PlanAssignment{
            .target_column = planner::PlanColumnIndex{1U},
            .expression = binary(
                types::BinaryOperator::kAdd,
                types::kInt64Type,
                input(1U, types::kInt64Type),
                literal(types::Value::integer(1))
            ),
        }
    );
    planner::ModifyTablePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = {}},
        .operation = planner::ModifyOperation::kUpdate,
        .target = target_relation(),
        .target_columns = {},
        .assignments = std::move(assignments),
        .source = planner::PlanQueryModificationSource{
            .input = std::make_unique<planner::Plan>(source_plan(schema)),
        },
    };
    RecordingPort port;
    TestFactory factory{schema, source_rows(), duplicate_rids()};
    auto state = ModifyTableState::create(plan, factory, port);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto emitted = state.value()->next(context.value());
    ASSERT_TRUE(emitted);
    EXPECT_TRUE(emitted.value());
    EXPECT_EQ(port.updates.size(), 1U);
    EXPECT_EQ(
        port.updates.front().new_values,
        (std::vector<types::Value>{types::Value::integer(7), types::Value::integer(11)})
    );
    EXPECT_FALSE(port.updates.front().indexed_columns_unchanged);
    EXPECT_EQ(state.value()->affected_rows(), 1U);
    auto exhausted = state.value()->next(context.value());
    ASSERT_TRUE(exhausted);
    EXPECT_FALSE(exhausted.value());
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(ModifyTableStateTest, DeletesDuplicateRidOnlyOnce) {
    const auto schema = target_schema();
    planner::ModifyTablePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = {}},
        .operation = planner::ModifyOperation::kDelete,
        .target = target_relation(),
        .target_columns = {},
        .assignments = {},
        .source = planner::PlanQueryModificationSource{
            .input = std::make_unique<planner::Plan>(source_plan(schema)),
        },
    };
    RecordingPort port;
    TestFactory factory{schema, source_rows(), duplicate_rids()};
    auto state = ModifyTableState::create(plan, factory, port);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto emitted = state.value()->next(context.value());
    ASSERT_TRUE(emitted);
    EXPECT_TRUE(emitted.value());
    EXPECT_EQ(port.deletes.size(), 1U);
    auto exhausted = state.value()->next(context.value());
    ASSERT_TRUE(exhausted);
    EXPECT_FALSE(exhausted.value());
    EXPECT_EQ(state.value()->affected_rows(), 1U);
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(ModifyTableStateTest, RejectsSourceDescriptorMismatchAtConstruction) {
    const auto target = target_schema();
    planner::PlanSchema source = target;
    source.columns[1].data_type = types::kBoolType;
    planner::ModifyTablePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = {}},
        .operation = planner::ModifyOperation::kDelete,
        .target = target_relation(),
        .target_columns = {},
        .assignments = {},
        .source = planner::PlanQueryModificationSource{
            .input = std::make_unique<planner::Plan>(source_plan(source)),
        },
    };
    RecordingPort port;
    TestFactory factory{source, {}, {}};

    auto state = ModifyTableState::create(plan, factory, port);

    ASSERT_FALSE(state);
    EXPECT_EQ(state.error().code(), base::ErrorCode::kExecution);
}

TEST(ModifyTableStateTest, RejectsDuplicateTargetColumns) {
    planner::ModifyTablePlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = {}},
        .operation = planner::ModifyOperation::kInsert,
        .target = target_relation(),
        .target_columns = {planner::PlanColumnIndex{0U}, planner::PlanColumnIndex{0U}},
        .assignments = {},
        .source = planner::PlanValuesModificationSource{
            .rows = {},
        },
    };
    std::vector<PlanExpressionPtr> duplicate_mapping_row;
    duplicate_mapping_row.push_back(literal(types::Value::integer(1)));
    duplicate_mapping_row.push_back(literal(types::Value::integer(2)));
    std::get<planner::PlanValuesModificationSource>(plan.source)
        .rows.push_back(std::move(duplicate_mapping_row));
    RecordingPort port;
    TestFactory factory{target_schema(), {}, {}};
    auto state = ModifyTableState::create(plan, factory, port);
    ASSERT_FALSE(state);
    EXPECT_EQ(state.error().code(), base::ErrorCode::kPlan);
}

}  // namespace
}  // namespace db::execution
