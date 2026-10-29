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
#include "db/execution/operators.hpp"
#include "db/planner/plan.hpp"
#include "db/types/data_type.hpp"
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

auto input(const std::uint32_t column, const types::DataType data_type) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanInputColumnReference{.column = planner::PlanColumnIndex{column}}
    );
}

auto parameter(const std::uint32_t id, const types::DataType data_type) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanParameterReference{
            .id = planner::PlanParameterId{id},
            .kind = planner::PlanParameterKind::kExecutor,
            .data_type = data_type,
        }
    );
}

auto binary(
    const types::BinaryOperator operation,
    const types::DataType data_type,
    PlanExpressionPtr left,
    PlanExpressionPtr right
) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        data_type,
        planner::PlanBinaryExpression{
            .op = operation,
            .left = std::move(left),
            .right = std::move(right),
        }
    );
}

auto equal_input_to(const std::int64_t expected) -> PlanExpressionPtr {
    return binary(
        types::BinaryOperator::kEqual,
        types::kBoolType,
        input(0U, types::kInt64Type),
        literal(types::Value::integer(expected))
    );
}

auto integer_descriptor() -> TupleDescriptorHandle {
    auto descriptor = TupleDescriptor::create({
        TupleColumn{.name = "id", .data_type = types::kInt64Type},
    });
    EXPECT_TRUE(descriptor) << descriptor.error().to_string();
    return std::move(descriptor).value();
}

auto integer_tuple(const std::int64_t value, const std::optional<base::Rid> rid = std::nullopt)
    -> base::Result<Tuple> {
    return Tuple::create(integer_descriptor(), {types::Value::integer(value)}, rid);
}

auto executor_context(const types::Value& value) -> base::Result<ExecContext> {
    auto namespace_result = planner::ParameterNamespace::from_external_slots({});
    if (!namespace_result) {
        return base::fail(std::move(namespace_result).error());
    }
    auto reference = namespace_result->allocate_executor_parameter(value.data_type());
    if (!reference) {
        return base::fail(std::move(reference).error());
    }
    auto result = ExecContext::create(namespace_result.value(), {});
    if (!result) {
        return base::fail(std::move(result).error());
    }
    auto bound = result->set_executor_parameter(reference.value(), value);
    if (!bound) {
        return base::fail(std::move(bound).error());
    }
    return std::move(result).value();
}

auto integer_schema() -> planner::PlanSchema {
    return planner::PlanSchema{
        .columns = {
            planner::PlanOutputColumn{
                .name = "id",
                .data_type = types::kInt64Type,
            },
        },
    };
}

auto integer_index_scan_plan() -> planner::IndexScanPlan {
    const auto schema = integer_schema();
    return planner::IndexScanPlan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{11U},
                .name = "items",
                .schema = schema,
            },
        .index =
            planner::PlanIndex{
                .oid = base::Oid{12U},
                .name = "items_id_idx",
                .keys = {{
                    .relation_column = planner::PlanColumnIndex{0U},
                    .column_name = "id",
                    .data_type = types::kInt64Type,
                }},
                .unique = true,
            },
        .bounds = {},
        .required_columns = std::vector{planner::PlanColumnIndex{0U}},
        .filter = nullptr,
    };
}

class FakeSeqPort final : public SeqScanPort {
public:
    explicit FakeSeqPort(std::vector<Tuple> rows, const bool move_rows = false)
        : rows_{std::move(rows)},
          move_rows_{move_rows} {}

    auto configure(SeqScanRequest request) -> base::Status override {
        request_ = std::move(request);
        configured_ = true;
        return {};
    }

    auto open(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        if (!configured_) {
            return base::fail(base::Error{base::ErrorCode::kExecution, "scan was not configured"});
        }
        cursor_ = 0U;
        opened_ = true;
        ++open_count_;
        if (open_failure_) {
            return base::fail(base::Error{base::ErrorCode::kIo, "injected scan open failure"});
        }
        return {};
    }

    auto next(ExecContext& context, std::vector<types::Value> recycled_values)
        -> base::Result<std::optional<Tuple>> override {
        static_cast<void>(context);
        static_cast<void>(recycled_values);
        if (cursor_ == rows_.size()) {
            return std::optional<Tuple>{};
        }
        if (move_rows_) {
            return std::optional<Tuple>{std::move(rows_[cursor_++])};
        }
        return std::optional<Tuple>{rows_[cursor_++]};
    }

    auto rescan(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        cursor_ = 0U;
        ++rescan_count_;
        return {};
    }

    auto close(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        opened_ = false;
        ++close_count_;
        return {};
    }

    auto open_count() const noexcept -> std::size_t {
        return open_count_;
    }

    auto rescan_count() const noexcept -> std::size_t {
        return rescan_count_;
    }

    auto close_count() const noexcept -> std::size_t {
        return close_count_;
    }

    auto opened() const noexcept -> bool {
        return opened_;
    }

    void set_open_failure(const bool enabled) noexcept {
        open_failure_ = enabled;
    }

private:
    std::vector<Tuple> rows_;
    SeqScanRequest request_;
    std::size_t cursor_{0U};
    std::size_t open_count_{0U};
    std::size_t rescan_count_{0U};
    std::size_t close_count_{0U};
    bool configured_{false};
    bool opened_{false};
    bool open_failure_{false};
    bool move_rows_{false};
};

class FakeIndexPort final : public IndexScanPort {
public:
    explicit FakeIndexPort(std::vector<Tuple> rows)
        : rows_{std::move(rows)} {}

    auto configure(IndexScanRequest request) -> base::Status override {
        request_ = std::move(request);
        return {};
    }

    auto bind(std::span<const IndexScanBound> bounds) -> base::Status override {
        bounds_.assign(bounds.begin(), bounds.end());
        ++bind_count_;
        return {};
    }

    auto open(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
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

    auto bounds() const noexcept -> std::span<const IndexScanBound> {
        return bounds_;
    }

    auto bind_count() const noexcept -> std::size_t {
        return bind_count_;
    }

private:
    std::vector<Tuple> rows_;
    IndexScanRequest request_;
    std::vector<IndexScanBound> bounds_;
    std::size_t cursor_{0U};
    std::size_t bind_count_{0U};
};

class RecyclingSeqPort final : public SeqScanPort {
public:
    RecyclingSeqPort(TupleDescriptorHandle descriptor, const std::size_t rows)
        : descriptor_{std::move(descriptor)},
          rows_{rows} {}

    auto configure(SeqScanRequest request) -> base::Status override {
        static_cast<void>(request);
        return {};
    }

    auto open(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        position_ = 0U;
        return {};
    }

    auto next(ExecContext& context, std::vector<types::Value> recycled_values)
        -> base::Result<std::optional<Tuple>> override {
        static_cast<void>(context);
        if (position_ == rows_) {
            return std::optional<Tuple>{};
        }
        incoming_capacities_.push_back(recycled_values.capacity());
        recycled_values.clear();
        recycled_values.push_back(types::Value::integer(static_cast<std::int64_t>(position_)));
        ++position_;
        auto tuple = Tuple::create(descriptor_, std::move(recycled_values));
        if (!tuple) {
            return base::fail(std::move(tuple).error());
        }
        return std::optional<Tuple>{std::move(tuple).value()};
    }

    auto rescan(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        position_ = 0U;
        return {};
    }

    auto close(ExecContext& context) -> base::Status override {
        static_cast<void>(context);
        return {};
    }

    auto incoming_capacities() const noexcept -> std::span<const std::size_t> {
        return incoming_capacities_;
    }

private:
    TupleDescriptorHandle descriptor_;
    std::size_t rows_;
    std::size_t position_{0U};
    std::vector<std::size_t> incoming_capacities_;
};

auto values_state() -> base::Result<std::unique_ptr<ValuesState>> {
    const auto schema = integer_schema();
    planner::ValuesPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .rows = {},
    };
    std::vector<PlanExpressionPtr> first;
    first.push_back(literal(types::Value::integer(1)));
    plan.rows.push_back(std::move(first));
    std::vector<PlanExpressionPtr> second;
    second.push_back(literal(types::Value::integer(2)));
    plan.rows.push_back(std::move(second));
    return ValuesState::create(plan);
}

TEST(OperatorsTest, ValuesPreservesOrderAndRescans) {
    auto state = values_state();
    ASSERT_TRUE(state) << state.error().to_string();
    auto execution_context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(execution_context) << execution_context.error().to_string();
    ASSERT_TRUE(state.value()->open(execution_context.value()));

    ASSERT_TRUE(state.value()->next(execution_context.value()));
    EXPECT_EQ(*state.value()->output_slot().tuple()->value_at(0U), types::Value::integer(1));
    const types::Value* const first_storage = state.value()->output_slot().tuple()->values().data();
    ASSERT_TRUE(state.value()->next(execution_context.value()));
    EXPECT_EQ(state.value()->output_slot().tuple()->values().data(), first_storage);
    EXPECT_EQ(*state.value()->output_slot().tuple()->value_at(0U), types::Value::integer(2));
    auto end = state.value()->next(execution_context.value());
    ASSERT_TRUE(end);
    EXPECT_FALSE(end.value());
    ASSERT_TRUE(state.value()->rescan(execution_context.value()));
    ASSERT_TRUE(state.value()->next(execution_context.value()));
    EXPECT_EQ(*state.value()->output_slot().tuple()->value_at(0U), types::Value::integer(1));
}

TEST(OperatorsTest, ResultProjectsChildAndOwnsOutputDescriptor) {
    auto child = values_state();
    ASSERT_TRUE(child) << child.error().to_string();
    const auto schema = planner::PlanSchema{
        .columns = {{.name = "computed", .data_type = types::kInt64Type}},
    };
    planner::ResultPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .input = nullptr,
        .projection = {},
    };
    plan.projection.push_back(
        planner::PlanProjectionItem{
            .expression = binary(
                types::BinaryOperator::kAdd,
                types::kInt64Type,
                input(0U, types::kInt64Type),
                literal(types::Value::integer(10))
            ),
            .output_name = "computed",
        }
    );
    auto state = ResultState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto execution_context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(execution_context);
    ASSERT_TRUE(state.value()->open(execution_context.value()));
    ASSERT_TRUE(state.value()->next(execution_context.value()));
    ASSERT_EQ(state.value()->output_slot().descriptor()->columns()[0U].name, "computed");
    EXPECT_EQ(*state.value()->output_slot().tuple()->value_at(0U), types::Value::integer(11));
}

TEST(OperatorsTest, ResultUsesCompiledLayoutForReorderedInputColumns) {
    const planner::PlanSchema input_schema{
        .columns = {
            {.name = "left", .data_type = types::kInt64Type},
            {.name = "right", .data_type = types::kInt64Type},
        },
    };
    planner::ValuesPlan values_plan{
        .metadata = planner::PlanOperatorMetadata{.output = input_schema},
        .rows = {},
    };
    std::vector<PlanExpressionPtr> row;
    row.push_back(literal(types::Value::integer(3)));
    row.push_back(literal(types::Value::integer(7)));
    values_plan.rows.push_back(std::move(row));
    auto child = ValuesState::create(values_plan);
    ASSERT_TRUE(child) << child.error().to_string();

    const planner::PlanSchema output_schema{
        .columns = {
            {.name = "first", .data_type = types::kInt64Type},
            {.name = "second", .data_type = types::kInt64Type},
        },
    };
    planner::ResultPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .projection = {},
    };
    plan.projection.push_back(
        planner::PlanProjectionItem{
            .expression = input(1U, types::kInt64Type),
            .output_name = "first",
        }
    );
    plan.projection.push_back(
        planner::PlanProjectionItem{
            .expression = input(0U, types::kInt64Type),
            .output_name = "second",
        }
    );
    auto state = ResultState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));
    const Tuple* output = state.value()->output_slot().tuple();
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(
        std::vector<types::Value>(output->values().begin(), output->values().end()),
        (std::vector<types::Value>{types::Value::integer(7), types::Value::integer(3)})
    );
    EXPECT_EQ(output->descriptor()->columns()[0U].name, "first");
}

TEST(OperatorsTest, ResultCompilesMixedProjectionAndReusesOutputStorage) {
    const types::DataType name_type{types::kCharTypeId, types::TypeModifier{8}};
    const planner::PlanSchema input_schema{
        .columns = {
            {.name = "id", .data_type = types::kInt64Type},
            {.name = "name", .data_type = name_type},
        },
    };
    planner::ValuesPlan values_plan{
        .metadata = planner::PlanOperatorMetadata{.output = input_schema},
        .rows = {},
    };
    for (const auto& [id, name] :
         std::vector<std::pair<std::int64_t, std::string>>{{3, "alice"}, {7, "bob"}}) {
        std::vector<PlanExpressionPtr> row;
        row.push_back(literal(types::Value::integer(id)));
        row.push_back(literal(types::Value::character(name, types::TypeModifier{8})));
        values_plan.rows.push_back(std::move(row));
    }
    auto child = ValuesState::create(values_plan);
    ASSERT_TRUE(child) << child.error().to_string();

    const planner::PlanSchema output_schema{
        .columns = {
            {.name = "first_name", .data_type = name_type},
            {.name = "computed", .data_type = types::kInt64Type},
            {.name = "second_name", .data_type = name_type},
        },
    };
    planner::ResultPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output_schema},
        .input = nullptr,
        .projection = {},
    };
    plan.projection.push_back(
        planner::PlanProjectionItem{
            .expression = input(1U, name_type),
            .output_name = "first_name",
        }
    );
    plan.projection.push_back(
        planner::PlanProjectionItem{
            .expression = binary(
                types::BinaryOperator::kAdd,
                types::kInt64Type,
                input(0U, types::kInt64Type),
                literal(types::Value::integer(10))
            ),
            .output_name = "computed",
        }
    );
    plan.projection.push_back(
        planner::PlanProjectionItem{
            .expression = input(1U, name_type),
            .output_name = "second_name",
        }
    );
    auto state = ResultState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));

    ASSERT_TRUE(state.value()->next(context.value()));
    const Tuple* output = state.value()->output_slot().tuple();
    ASSERT_NE(output, nullptr);
    const types::Value* const first_storage = output->values().data();
    EXPECT_EQ(
        std::vector<types::Value>(output->values().begin(), output->values().end()),
        (std::vector<types::Value>{
            types::Value::character("alice", types::TypeModifier{8}),
            types::Value::integer(13),
            types::Value::character("alice", types::TypeModifier{8}),
        })
    );

    ASSERT_TRUE(state.value()->next(context.value()));
    output = state.value()->output_slot().tuple();
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->values().data(), first_storage);
    EXPECT_EQ(
        std::vector<types::Value>(output->values().begin(), output->values().end()),
        (std::vector<types::Value>{
            types::Value::character("bob", types::TypeModifier{8}),
            types::Value::integer(17),
            types::Value::character("bob", types::TypeModifier{8}),
        })
    );
}

TEST(OperatorsTest, ResultTransfersIdentityValuesButDoesNotExposeTheInputRid) {
    std::vector<Tuple> rows;
    rows.push_back(integer_tuple(19, base::Rid{base::PageId{6U}, base::SlotId{2U}}).value());
    auto child_port = std::make_unique<FakeSeqPort>(std::move(rows));
    const auto schema = integer_schema();
    planner::SeqScanPlan scan_plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{10U},
                .name = "items",
                .schema = schema,
            },
        .required_columns = std::nullopt,
        .filter = nullptr,
    };
    auto child = SeqScanState::create(scan_plan, std::move(child_port));
    ASSERT_TRUE(child) << child.error().to_string();
    planner::ResultPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .input = nullptr,
        .projection = {},
    };
    plan.projection.push_back(
        planner::PlanProjectionItem{
            .expression = input(0U, types::kInt64Type),
            .output_name = "id",
        }
    );
    auto state = ResultState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));
    const Tuple* output = state.value()->output_slot().tuple();
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->values()[0U], types::Value::integer(19));
    EXPECT_FALSE(output->rid().has_value());
}

TEST(OperatorsTest, FusedSeqScanFiltersBeforeMixedProjectionAndClearsRid) {
    std::vector<Tuple> rows;
    rows.push_back(integer_tuple(1, base::Rid{base::PageId{8U}, base::SlotId{1U}}).value());
    rows.push_back(integer_tuple(2, base::Rid{base::PageId{8U}, base::SlotId{2U}}).value());
    auto port = std::make_unique<FakeSeqPort>(std::move(rows));
    auto* const port_view = port.get();
    const auto scan_schema = integer_schema();
    planner::SeqScanPlan scan{
        .metadata = planner::PlanOperatorMetadata{.output = scan_schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{10U},
                .name = "items",
                .schema = scan_schema,
            },
        .required_columns = std::vector{planner::PlanColumnIndex{0U}},
        .filter = equal_input_to(2),
    };
    const planner::PlanSchema result_schema{
        .columns = {
            {.name = "id", .data_type = types::kInt64Type},
            {.name = "quotient", .data_type = types::kInt64Type},
        },
    };
    planner::ResultPlan result{
        .metadata = planner::PlanOperatorMetadata{.output = result_schema},
        .input = nullptr,
        .projection = {},
    };
    result.projection.push_back(
        planner::PlanProjectionItem{
            .expression = input(0U, types::kInt64Type),
            .output_name = "id",
        }
    );
    result.projection.push_back(
        planner::PlanProjectionItem{
            .expression = binary(
                types::BinaryOperator::kDivide,
                types::kInt64Type,
                literal(types::Value::integer(10)),
                binary(
                    types::BinaryOperator::kSubtract,
                    types::kInt64Type,
                    input(0U, types::kInt64Type),
                    literal(types::Value::integer(1))
                )
            ),
            .output_name = "quotient",
        }
    );
    auto state = SeqScanState::create_projected(scan, result, std::move(port));
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));

    auto produced = state.value()->next(context.value());
    ASSERT_TRUE(produced) << produced.error().to_string();
    ASSERT_TRUE(produced.value());
    const Tuple* output = state.value()->output_slot().tuple();
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->descriptor()->size(), 2U);
    EXPECT_EQ(output->descriptor()->columns()[1U].name, "quotient");
    EXPECT_EQ(output->values()[0U], types::Value::integer(2));
    EXPECT_EQ(output->values()[1U], types::Value::integer(10));
    EXPECT_FALSE(output->rid().has_value());

    ASSERT_TRUE(state.value()->rescan(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));
    EXPECT_EQ(port_view->open_count(), 1U);
    EXPECT_EQ(port_view->rescan_count(), 1U);
}

TEST(OperatorsTest, FusedProjectionCopiesADirectColumnNeededByALaterExpression) {
    const types::TypeModifier text_modifier{128};
    const types::DataType text_type{types::kCharTypeId, text_modifier};
    const planner::PlanSchema scan_schema{
        .columns = {{.name = "payload", .data_type = text_type}},
    };
    auto descriptor = TupleDescriptor::from_plan_schema(scan_schema);
    ASSERT_TRUE(descriptor) << descriptor.error().to_string();
    const std::string payload(96U, 'x');
    auto source = Tuple::create(
        descriptor.value(),
        {types::Value::character(payload, text_modifier)},
        base::Rid{base::PageId{8U}, base::SlotId{3U}}
    );
    ASSERT_TRUE(source) << source.error().to_string();
    const char* const source_storage = source->values()[0U].as_string()->data();
    std::vector<Tuple> rows;
    rows.push_back(std::move(source).value());
    auto port = std::make_unique<FakeSeqPort>(std::move(rows), true);

    planner::SeqScanPlan scan{
        .metadata = planner::PlanOperatorMetadata{.output = scan_schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{10U},
                .name = "items",
                .schema = scan_schema,
            },
        .required_columns = std::vector{planner::PlanColumnIndex{0U}},
        .filter = nullptr,
    };
    const planner::PlanSchema result_schema{
        .columns = {
            {.name = "payload", .data_type = text_type},
            {.name = "matches", .data_type = types::kBoolType},
        },
    };
    planner::ResultPlan result{
        .metadata = planner::PlanOperatorMetadata{.output = result_schema},
        .input = nullptr,
        .projection = {},
    };
    result.projection.push_back(
        planner::PlanProjectionItem{
            .expression = input(0U, text_type),
            .output_name = "payload",
        }
    );
    result.projection.push_back(
        planner::PlanProjectionItem{
            .expression = binary(
                types::BinaryOperator::kEqual,
                types::kBoolType,
                input(0U, text_type),
                literal(types::Value::character(payload, text_modifier))
            ),
            .output_name = "matches",
        }
    );

    auto state = SeqScanState::create_projected(scan, result, std::move(port));
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));

    const Tuple* const output = state.value()->output_slot().tuple();
    ASSERT_NE(output, nullptr);
    ASSERT_NE(output->values()[0U].as_string(), nullptr);
    ASSERT_NE(output->values()[1U].as_boolean(), nullptr);
    EXPECT_EQ(*output->values()[0U].as_string(), payload);
    EXPECT_TRUE(*output->values()[1U].as_boolean());
    EXPECT_NE(output->values()[0U].as_string()->data(), source_storage);
    EXPECT_FALSE(output->rid().has_value());
}

TEST(OperatorsTest, FusedProjectionMovesOnlyTheLastRepeatedDirectColumn) {
    const types::TypeModifier text_modifier{128};
    const types::DataType text_type{types::kCharTypeId, text_modifier};
    const planner::PlanSchema scan_schema{
        .columns = {{.name = "payload", .data_type = text_type}},
    };
    auto descriptor = TupleDescriptor::from_plan_schema(scan_schema);
    ASSERT_TRUE(descriptor) << descriptor.error().to_string();
    const std::string payload(96U, 'y');
    auto source = Tuple::create(
        descriptor.value(),
        {types::Value::character(payload, text_modifier)},
        base::Rid{base::PageId{8U}, base::SlotId{4U}}
    );
    ASSERT_TRUE(source) << source.error().to_string();
    const char* const source_storage = source->values()[0U].as_string()->data();
    std::vector<Tuple> rows;
    rows.push_back(std::move(source).value());
    auto port = std::make_unique<FakeSeqPort>(std::move(rows), true);

    planner::SeqScanPlan scan{
        .metadata = planner::PlanOperatorMetadata{.output = scan_schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{10U},
                .name = "items",
                .schema = scan_schema,
            },
        .required_columns = std::vector{planner::PlanColumnIndex{0U}},
        .filter = nullptr,
    };
    const planner::PlanSchema result_schema{
        .columns = {
            {.name = "first_payload", .data_type = text_type},
            {.name = "matches", .data_type = types::kBoolType},
            {.name = "last_payload", .data_type = text_type},
        },
    };
    planner::ResultPlan result{
        .metadata = planner::PlanOperatorMetadata{.output = result_schema},
        .input = nullptr,
        .projection = {},
    };
    result.projection.push_back(
        planner::PlanProjectionItem{
            .expression = input(0U, text_type),
            .output_name = "first_payload",
        }
    );
    result.projection.push_back(
        planner::PlanProjectionItem{
            .expression = binary(
                types::BinaryOperator::kEqual,
                types::kBoolType,
                input(0U, text_type),
                literal(types::Value::character(payload, text_modifier))
            ),
            .output_name = "matches",
        }
    );
    result.projection.push_back(
        planner::PlanProjectionItem{
            .expression = input(0U, text_type),
            .output_name = "last_payload",
        }
    );

    auto state = SeqScanState::create_projected(scan, result, std::move(port));
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));

    const Tuple* const output = state.value()->output_slot().tuple();
    ASSERT_NE(output, nullptr);
    ASSERT_NE(output->values()[0U].as_string(), nullptr);
    ASSERT_NE(output->values()[1U].as_boolean(), nullptr);
    ASSERT_NE(output->values()[2U].as_string(), nullptr);
    EXPECT_EQ(*output->values()[0U].as_string(), payload);
    EXPECT_TRUE(*output->values()[1U].as_boolean());
    EXPECT_EQ(*output->values()[2U].as_string(), payload);
    EXPECT_NE(output->values()[0U].as_string()->data(), source_storage);
    EXPECT_EQ(output->values()[2U].as_string()->data(), source_storage);
    EXPECT_FALSE(output->rid().has_value());
}

TEST(OperatorsTest, FusedSeqScanIdentityProjectionRetagsValuesWithoutRid) {
    std::vector<Tuple> rows;
    rows.push_back(integer_tuple(19, base::Rid{base::PageId{9U}, base::SlotId{3U}}).value());
    auto port = std::make_unique<FakeSeqPort>(std::move(rows));
    const auto schema = integer_schema();
    planner::SeqScanPlan scan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{10U},
                .name = "items",
                .schema = schema,
            },
        .required_columns = std::vector{planner::PlanColumnIndex{0U}},
        .filter = nullptr,
    };
    planner::ResultPlan result{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .input = nullptr,
        .projection = {},
    };
    result.projection.push_back(
        planner::PlanProjectionItem{
            .expression = input(0U, types::kInt64Type),
            .output_name = "id",
        }
    );
    auto state = SeqScanState::create_projected(scan, result, std::move(port));
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));
    const Tuple* output = state.value()->output_slot().tuple();
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->values()[0U], types::Value::integer(19));
    EXPECT_FALSE(output->rid().has_value());
}

TEST(OperatorsTest, FusedProjectionKeepsScanStorageWhenConsumerTakesOutput) {
    const auto schema = integer_schema();
    auto descriptor = TupleDescriptor::from_plan_schema(schema);
    ASSERT_TRUE(descriptor) << descriptor.error().to_string();
    auto port = std::make_unique<RecyclingSeqPort>(descriptor.value(), 2U);
    auto* const port_view = port.get();
    planner::SeqScanPlan scan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{10U},
                .name = "items",
                .schema = schema,
            },
        .required_columns = std::vector{planner::PlanColumnIndex{0U}},
        .filter = nullptr,
    };
    const planner::PlanSchema result_schema{
        .columns = {{.name = "next_id", .data_type = types::kInt64Type}},
    };
    planner::ResultPlan result{
        .metadata = planner::PlanOperatorMetadata{.output = result_schema},
        .input = nullptr,
        .projection = {},
    };
    result.projection.push_back(
        planner::PlanProjectionItem{
            .expression = binary(
                types::BinaryOperator::kAdd,
                types::kInt64Type,
                input(0U, types::kInt64Type),
                literal(types::Value::integer(1))
            ),
            .output_name = "next_id",
        }
    );
    auto state = SeqScanState::create_projected(scan, result, std::move(port));
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));

    std::vector<std::vector<types::Value>> consumed_rows;
    for (std::int64_t expected = 1; expected <= 2; ++expected) {
        ASSERT_TRUE(state.value()->next(context.value()));
        auto values = state.value()->take_output_values();
        ASSERT_TRUE(values.has_value());
        ASSERT_EQ(values->size(), 1U);
        EXPECT_EQ(values->front(), types::Value::integer(expected));
        consumed_rows.push_back(std::move(*values));
    }
    const auto capacities = port_view->incoming_capacities();
    ASSERT_EQ(capacities.size(), 2U);
    EXPECT_EQ(capacities[0U], 0U);
    EXPECT_GE(capacities[1U], 1U);
    EXPECT_EQ(consumed_rows.size(), 2U);
}

TEST(OperatorsTest, SeqScanRejectsFalseAndUnknownButKeepsRid) {
    std::vector<Tuple> rows;
    rows.push_back(integer_tuple(1, base::Rid{base::PageId{3U}, base::SlotId{1U}}).value());
    rows.push_back(integer_tuple(2, base::Rid{base::PageId{3U}, base::SlotId{2U}}).value());
    auto port = std::make_unique<FakeSeqPort>(std::move(rows));
    auto* port_view = port.get();
    const auto schema = integer_schema();
    planner::SeqScanPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{10U},
                .name = "items",
                .schema = schema,
            },
        .required_columns = std::vector{planner::PlanColumnIndex{0U}},
        .filter = equal_input_to(2),
    };
    auto state = SeqScanState::create(plan, std::move(port));
    ASSERT_TRUE(state) << state.error().to_string();
    auto execution_context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(execution_context);
    ASSERT_TRUE(state.value()->open(execution_context.value()));
    ASSERT_TRUE(state.value()->next(execution_context.value()));
    EXPECT_EQ(*state.value()->output_slot().tuple()->value_at(0U), types::Value::integer(2));
    const auto expected_rid = base::Rid{base::PageId{3U}, base::SlotId{2U}};
    EXPECT_EQ(state.value()->output_slot().tuple()->rid(), expected_rid);
    auto end = state.value()->next(execution_context.value());
    ASSERT_TRUE(end);
    EXPECT_FALSE(end.value());
    ASSERT_TRUE(state.value()->rescan(execution_context.value()));
    ASSERT_TRUE(state.value()->next(execution_context.value()));
    EXPECT_EQ(port_view->open_count(), 1U);
    EXPECT_EQ(port_view->rescan_count(), 1U);
    EXPECT_EQ(port_view->close_count(), 0U);
}

TEST(OperatorsTest, SeqScanRechecksAChangedExternalDescriptor) {
    auto valid_descriptor = TupleDescriptor::create({
        TupleColumn{.name = "id", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(valid_descriptor) << valid_descriptor.error().to_string();
    auto valid_tuple = Tuple::create(valid_descriptor.value(), {types::Value::integer(1)});
    ASSERT_TRUE(valid_tuple) << valid_tuple.error().to_string();

    auto invalid_descriptor = TupleDescriptor::create({
        TupleColumn{.name = "id", .data_type = types::kBoolType},
    });
    ASSERT_TRUE(invalid_descriptor) << invalid_descriptor.error().to_string();
    auto invalid_tuple = Tuple::create(invalid_descriptor.value(), {types::Value::boolean(true)});
    ASSERT_TRUE(invalid_tuple) << invalid_tuple.error().to_string();

    std::vector<Tuple> rows;
    rows.push_back(std::move(valid_tuple).value());
    rows.push_back(std::move(invalid_tuple).value());
    const auto schema = integer_schema();
    planner::SeqScanPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{10U},
                .name = "items",
                .schema = schema,
            },
        .required_columns = std::nullopt,
        .filter = nullptr,
    };
    auto state = SeqScanState::create(plan, std::make_unique<FakeSeqPort>(std::move(rows)));
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));
    const auto second = state.value()->next(context.value());
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code(), base::ErrorCode::kExecution);
}

TEST(OperatorsTest, SeqScanRetagsOwnedPortRowsAndStillRejectsInvalidShapes) {
    auto source_descriptor = TupleDescriptor::create({
        TupleColumn{.name = "source_id", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(source_descriptor) << source_descriptor.error().to_string();
    auto source_tuple = Tuple::create(
        source_descriptor.value(),
        {types::Value::integer(11)},
        base::Rid{base::PageId{4U}, base::SlotId{7U}}
    );
    ASSERT_TRUE(source_tuple) << source_tuple.error().to_string();

    planner::SeqScanPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = integer_schema()},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{10U},
                .name = "items",
                .schema = integer_schema(),
            },
        .required_columns = std::nullopt,
        .filter = nullptr,
    };
    std::vector<Tuple> rows;
    rows.push_back(std::move(source_tuple).value());
    auto state = SeqScanState::create(plan, std::make_unique<FakeSeqPort>(std::move(rows)));
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));
    const Tuple* output = state.value()->output_slot().tuple();
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->descriptor().get(), state.value()->output_slot().descriptor().get());
    EXPECT_EQ(output->descriptor()->columns()[0U].name, "id");
    EXPECT_EQ(output->values()[0U], types::Value::integer(11));

    auto invalid_descriptor = TupleDescriptor::create({
        TupleColumn{.name = "id", .data_type = types::kBoolType},
    });
    ASSERT_TRUE(invalid_descriptor) << invalid_descriptor.error().to_string();
    auto invalid_tuple = Tuple::create(invalid_descriptor.value(), {types::Value::boolean(true)});
    ASSERT_TRUE(invalid_tuple) << invalid_tuple.error().to_string();
    std::vector<Tuple> invalid_rows;
    invalid_rows.push_back(std::move(invalid_tuple).value());
    auto invalid_state =
        SeqScanState::create(plan, std::make_unique<FakeSeqPort>(std::move(invalid_rows)));
    ASSERT_TRUE(invalid_state) << invalid_state.error().to_string();
    ASSERT_TRUE(invalid_state.value()->open(context.value()));
    const auto invalid_next = invalid_state.value()->next(context.value());
    ASSERT_FALSE(invalid_next);
    EXPECT_EQ(invalid_next.error().code(), base::ErrorCode::kExecution);
}

TEST(OperatorsTest, FailedScanOpenIsClosedThroughThePlanLifecycle) {
    std::vector<Tuple> rows;
    auto port = std::make_unique<FakeSeqPort>(std::move(rows));
    auto* const port_view = port.get();
    port_view->set_open_failure(true);
    const auto schema = integer_schema();
    planner::SeqScanPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = schema},
        .relation =
            planner::PlanRelation{
                .oid = base::Oid{10U},
                .name = "items",
                .schema = schema,
            },
        .required_columns = std::nullopt,
        .filter = nullptr,
    };
    auto state = SeqScanState::create(plan, std::move(port));
    ASSERT_TRUE(state) << state.error().to_string();
    auto execution_context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(execution_context) << execution_context.error().to_string();

    const auto opened = state.value()->open(execution_context.value());
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(state.value()->lifecycle(), PlanStateLifecycle::kFailed);
    EXPECT_TRUE(port_view->opened());
    EXPECT_EQ(port_view->close_count(), 0U);

    ASSERT_TRUE(state.value()->close(execution_context.value()));
    EXPECT_EQ(state.value()->lifecycle(), PlanStateLifecycle::kClosed);
    EXPECT_FALSE(port_view->opened());
    EXPECT_EQ(port_view->close_count(), 1U);
}

TEST(OperatorsTest, IndexScanReevaluatesTypedBoundsOnRescan) {
    std::vector<Tuple> rows;
    rows.push_back(integer_tuple(7).value());
    auto port = std::make_unique<FakeIndexPort>(std::move(rows));
    auto* port_view = port.get();
    auto plan = integer_index_scan_plan();
    plan.bounds.push_back(
        planner::PlanIndexBound{
            .key_index = planner::PlanIndexKeyIndex{0U},
            .comparison = types::BinaryOperator::kEqual,
            .value = parameter(0U, types::kInt64Type),
        }
    );
    auto state = IndexScanState::create(plan, std::move(port));
    ASSERT_TRUE(state) << state.error().to_string();
    auto execution_context = executor_context(types::Value::integer(7));
    ASSERT_TRUE(execution_context) << execution_context.error().to_string();
    ASSERT_TRUE(state.value()->open(execution_context.value()));
    ASSERT_EQ(port_view->bounds().size(), 1U);
    EXPECT_EQ(port_view->bounds()[0U].key_index, ScanKeyIndex{0U});
    EXPECT_EQ(port_view->bounds()[0U].value, types::Value::integer(7));
    ASSERT_TRUE(state.value()->next(execution_context.value()));
    auto end = state.value()->next(execution_context.value());
    ASSERT_TRUE(end);
    EXPECT_FALSE(end.value());
    ASSERT_TRUE(execution_context->set_executor_parameter(
        planner::PlanParameterReference{
            .id = planner::PlanParameterId{0U},
            .kind = planner::PlanParameterKind::kExecutor,
            .data_type = types::kInt64Type,
        },
        types::Value::integer(9)
    ));
    ASSERT_TRUE(state.value()->rescan(execution_context.value()));
    EXPECT_EQ(port_view->bounds()[0U].value, types::Value::integer(9));
    EXPECT_EQ(port_view->bind_count(), 2U);
}

TEST(OperatorsTest, FusedIndexScanProjectsAndRebindsCurrentParameter) {
    std::vector<Tuple> rows;
    rows.push_back(integer_tuple(7, base::Rid{base::PageId{10U}, base::SlotId{4U}}).value());
    auto port = std::make_unique<FakeIndexPort>(std::move(rows));
    auto* const port_view = port.get();
    auto scan = integer_index_scan_plan();
    scan.bounds.push_back(
        planner::PlanIndexBound{
            .key_index = planner::PlanIndexKeyIndex{0U},
            .comparison = types::BinaryOperator::kEqual,
            .value = parameter(0U, types::kInt64Type),
        }
    );
    const planner::PlanSchema result_schema{
        .columns = {{.name = "next_id", .data_type = types::kInt64Type}},
    };
    planner::ResultPlan result{
        .metadata = planner::PlanOperatorMetadata{.output = result_schema},
        .input = nullptr,
        .projection = {},
    };
    result.projection.push_back(
        planner::PlanProjectionItem{
            .expression = binary(
                types::BinaryOperator::kAdd,
                types::kInt64Type,
                input(0U, types::kInt64Type),
                literal(types::Value::integer(1))
            ),
            .output_name = "next_id",
        }
    );
    auto state = IndexScanState::create_projected(scan, result, std::move(port));
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(7));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));
    const Tuple* output = state.value()->output_slot().tuple();
    ASSERT_NE(output, nullptr);
    EXPECT_EQ(output->values()[0U], types::Value::integer(8));
    EXPECT_FALSE(output->rid().has_value());

    ASSERT_TRUE(context->set_executor_parameter(
        planner::PlanParameterReference{
            .id = planner::PlanParameterId{0U},
            .kind = planner::PlanParameterKind::kExecutor,
            .data_type = types::kInt64Type,
        },
        types::Value::integer(9)
    ));
    ASSERT_TRUE(state.value()->rescan(context.value()));
    ASSERT_EQ(port_view->bounds().size(), 1U);
    EXPECT_EQ(port_view->bounds()[0U].value, types::Value::integer(9));
    EXPECT_EQ(port_view->bind_count(), 2U);
}

TEST(OperatorsTest, IndexScanRechecksAChangedExternalDescriptor) {
    auto valid_descriptor = TupleDescriptor::create({
        TupleColumn{.name = "id", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(valid_descriptor) << valid_descriptor.error().to_string();
    auto first = Tuple::create(valid_descriptor.value(), {types::Value::integer(1)});
    ASSERT_TRUE(first) << first.error().to_string();
    auto second = Tuple::create(valid_descriptor.value(), {types::Value::integer(2)});
    ASSERT_TRUE(second) << second.error().to_string();

    auto invalid_descriptor = TupleDescriptor::create({
        TupleColumn{.name = "id", .data_type = types::kBoolType},
    });
    ASSERT_TRUE(invalid_descriptor) << invalid_descriptor.error().to_string();
    auto invalid = Tuple::create(invalid_descriptor.value(), {types::Value::boolean(true)});
    ASSERT_TRUE(invalid) << invalid.error().to_string();

    std::vector<Tuple> rows;
    rows.push_back(std::move(first).value());
    rows.push_back(std::move(second).value());
    rows.push_back(std::move(invalid).value());
    auto state = IndexScanState::create(
        integer_index_scan_plan(),
        std::make_unique<FakeIndexPort>(std::move(rows))
    );
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = executor_context(types::Value::integer(0));
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));

    ASSERT_TRUE(state.value()->next(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));
    const auto changed_descriptor = state.value()->next(context.value());
    ASSERT_FALSE(changed_descriptor);
    EXPECT_EQ(changed_descriptor.error().code(), base::ErrorCode::kExecution);
}

}  // namespace
}  // namespace db::execution
