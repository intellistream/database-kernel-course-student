#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/execution/operators.hpp"
#include "db/execution/subplan.hpp"
#include "db/execution/subquery_scan.hpp"
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

auto parameter(const planner::PlanParameterReference& reference) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        reference.data_type,
        reference
    );
}

auto subplan_reference(const std::uint32_t index, const types::DataType type) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        type,
        planner::PlanSubPlanReference{.index = planner::PlanSubPlanIndex{index}}
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

auto is_null(PlanExpressionPtr operand) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kBoolType,
        planner::PlanIsNullExpression{.negated = false, .operand = std::move(operand)}
    );
}

auto schema(std::initializer_list<std::pair<const char*, types::DataType>> columns)
    -> planner::PlanSchema {
    planner::PlanSchema result;
    for (const auto& [name, type] : columns) {
        result.columns.push_back({.name = name, .data_type = type});
    }
    return result;
}

auto values_plan(const planner::PlanSchema& output, std::vector<std::vector<types::Value>> rows)
    -> planner::ValuesPlan {
    planner::ValuesPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .rows = {},
    };
    for (std::vector<types::Value>& row : rows) {
        std::vector<PlanExpressionPtr> expressions;
        expressions.reserve(row.size());
        for (types::Value& value : row) {
            expressions.push_back(literal(std::move(value)));
        }
        plan.rows.push_back(std::move(expressions));
    }
    return plan;
}

auto context_for(
    std::span<const types::DataType> executor_types,
    std::span<const types::Value> external_values = {}
) -> base::Result<ExecContext> {
    auto namespace_result = planner::ParameterNamespace::from_external_slots({});
    if (!namespace_result) {
        return base::fail(std::move(namespace_result).error());
    }
    for (const types::DataType type : executor_types) {
        auto reference = namespace_result->allocate_executor_parameter(type);
        if (!reference) {
            return base::fail(std::move(reference).error());
        }
    }
    return ExecContext::create(namespace_result.value(), external_values);
}

auto empty_context() -> base::Result<ExecContext> {
    return context_for({});
}

class ValuesFactory final : public PlanStateFactory {
public:
    auto create(const planner::Plan& plan) -> base::Result<std::unique_ptr<PlanState>> override {
        const auto* const values = std::get_if<planner::ValuesPlan>(&plan.node);
        if (values == nullptr) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kUnsupported,
                    "test factory only creates VALUES plans",
                }
            );
        }
        auto state = ValuesState::create(*values);
        if (!state) {
            return base::fail(std::move(state).error());
        }
        return std::unique_ptr<PlanState>{std::move(state).value()};
    }
};

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
                    "test state produced no output tuple",
                }
            );
        }
        rows.emplace_back(tuple->values().begin(), tuple->values().end());
    }
}

auto make_scan(
    const planner::PlanSchema& output,
    std::vector<std::vector<types::Value>> rows,
    PlanExpressionPtr filter
) -> base::Result<std::unique_ptr<SubqueryScanState>> {
    auto child_plan = values_plan(output, std::move(rows));
    auto child = ValuesState::create(child_plan);
    if (!child) {
        return base::fail(std::move(child).error());
    }
    planner::SubqueryScanPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .subquery = nullptr,
        .alias = "derived",
        .filter = std::move(filter),
    };
    return SubqueryScanState::create(plan, std::move(child).value());
}

TEST(SubqueryScanStateTest, AppliesTrueQualificationAndRescans) {
    const auto output = schema({{"id", types::kInt64Type}});
    auto state = make_scan(
        output,
        {{types::Value::integer(1)}, {types::Value::integer(2)}},
        binary(
            types::BinaryOperator::kEqual,
            types::kBoolType,
            input(0U, types::kInt64Type),
            literal(types::Value::integer(2))
        )
    );
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows);
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->front(), (std::vector<types::Value>{types::Value::integer(2)}));
    ASSERT_TRUE(state.value()->rescan(context.value()));
    auto rescanned = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rescanned);
    EXPECT_EQ(rescanned.value(), rows.value());
    ASSERT_TRUE(state.value()->close(context.value()));

    const auto mismatched_output = schema({{"id", types::kBoolType}});
    auto child = ValuesState::create(values_plan(output, {}));
    ASSERT_TRUE(child);
    planner::SubqueryScanPlan mismatched_plan{
        .metadata = planner::PlanOperatorMetadata{.output = mismatched_output},
        .subquery = nullptr,
        .alias = "derived",
        .filter = nullptr,
    };
    auto mismatched = SubqueryScanState::create(mismatched_plan, std::move(child).value());
    ASSERT_FALSE(mismatched);
    EXPECT_EQ(mismatched.error().code(), base::ErrorCode::kPlan);
}

TEST(SubqueryScanStateTest, TransfersRowsIntoItsOwnOutputDescriptor) {
    const auto child_output = schema({{"source_id", types::kInt64Type}});
    const auto output = schema({{"derived_id", types::kInt64Type}});
    auto child = ValuesState::create(values_plan(child_output, {{types::Value::integer(9)}}));
    ASSERT_TRUE(child) << child.error().to_string();
    planner::SubqueryScanPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .subquery = nullptr,
        .alias = "derived",
        .filter = nullptr,
    };
    auto state = SubqueryScanState::create(plan, std::move(child).value());
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    ASSERT_TRUE(state.value()->open(context.value()));
    ASSERT_TRUE(state.value()->next(context.value()));
    const Tuple* tuple = state.value()->output_slot().tuple();
    ASSERT_NE(tuple, nullptr);
    EXPECT_EQ(tuple->descriptor().get(), state.value()->output_slot().descriptor().get());
    EXPECT_EQ(tuple->descriptor()->columns()[0U].name, "derived_id");
    EXPECT_EQ(tuple->values()[0U], types::Value::integer(9));
}

TEST(SubPlanEvalStateTest, ScalarSubplanBindsArgumentsAndAllowsLaterReferences) {
    const auto output = schema({{"id", types::kInt64Type}});
    const auto child_output = schema({{"value", types::kInt64Type}});
    auto namespace_result = planner::ParameterNamespace::from_external_slots({});
    ASSERT_TRUE(namespace_result);
    auto parameter_result = namespace_result->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(parameter_result);
    const planner::PlanParameterReference parameter_reference = parameter_result.value();

    auto child_plan = values_plan(child_output, {{types::Value::integer(0)}});
    // Replace the literal child expression with the executor parameter.
    child_plan.rows.front().front() = parameter(parameter_reference);
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .subplans = {},
        .filter = binary(
            types::BinaryOperator::kEqual,
            types::kBoolType,
            subplan_reference(0U, types::kInt64Type),
            input(0U, types::kInt64Type)
        ),
    };
    std::vector<PlanExpressionPtr> correlated_arguments;
    correlated_arguments.push_back(input(0U, types::kInt64Type));
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(std::move(child_plan)),
            .strategy = planner::PlanSubPlanStrategy::kScalar,
            .input_expression = nullptr,
            .arguments = std::move(correlated_arguments),
            .parameters = {parameter_reference},
            .uses_inherited_parameters = false,
        }
    );
    auto child_input_plan =
        values_plan(output, {{types::Value::integer(1)}, {types::Value::integer(2)}});
    auto input_state = ValuesState::create(child_input_plan);
    ASSERT_TRUE(input_state);
    ValuesFactory factory;
    auto state = SubPlanEvalState::create(plan, std::move(input_state).value(), factory);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = ExecContext::create(namespace_result.value(), {});
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows);
    ASSERT_EQ(rows->size(), 2U);
    EXPECT_EQ(rows->at(0U), (std::vector<types::Value>{types::Value::integer(1)}));
    EXPECT_EQ(rows->at(1U), (std::vector<types::Value>{types::Value::integer(2)}));
    EXPECT_FALSE(context->executor_parameter_is_bound(parameter_reference).value());
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(SubPlanEvalStateTest, CleanupFailureIsAuthoritativeAndClearsValidBindings) {
    const auto output = schema({{"id", types::kInt64Type}});
    const auto child_output = schema({{"value", types::kInt64Type}});
    auto plan_namespace = planner::ParameterNamespace::from_external_slots({});
    ASSERT_TRUE(plan_namespace);
    auto first_parameter = plan_namespace->allocate_executor_parameter(types::kInt64Type);
    auto missing_parameter = plan_namespace->allocate_executor_parameter(types::kInt64Type);
    ASSERT_TRUE(first_parameter);
    ASSERT_TRUE(missing_parameter);

    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .subplans = {},
        .filter = nullptr,
    };
    std::vector<PlanExpressionPtr> arguments;
    arguments.push_back(input(0U, types::kInt64Type));
    arguments.push_back(input(0U, types::kInt64Type));
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(
                values_plan(child_output, {{types::Value::integer(1)}})
            ),
            .strategy = planner::PlanSubPlanStrategy::kScalar,
            .input_expression = nullptr,
            .arguments = std::move(arguments),
            .parameters = {first_parameter.value(), missing_parameter.value()},
            .uses_inherited_parameters = false,
        }
    );
    auto input_state = ValuesState::create(values_plan(output, {{types::Value::integer(9)}}));
    ASSERT_TRUE(input_state);
    ValuesFactory factory;
    auto state = SubPlanEvalState::create(plan, std::move(input_state).value(), factory);
    ASSERT_TRUE(state) << state.error().to_string();

    const std::array context_types{types::kInt64Type};
    auto context = context_for(context_types);
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto next = state.value()->next(context.value());
    ASSERT_FALSE(next);
    EXPECT_EQ(next.error().code(), base::ErrorCode::kExecution);
    EXPECT_NE(
        next.error().to_string().find("clear subplan parameters after binding failure"),
        std::string::npos
    );
    EXPECT_NE(next.error().to_string().find("original subplan failure"), std::string::npos);
    auto first_bound = context->executor_parameter_is_bound(first_parameter.value());
    ASSERT_TRUE(first_bound);
    EXPECT_FALSE(first_bound.value());
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(SubPlanEvalStateTest, MembershipUsesSqlNullSemanticsAndRescanClearsCache) {
    const auto output = schema({{"id", types::kInt64Type}});
    const auto child_output = schema({{"value", types::kInt64Type}});
    auto child_plan = values_plan(
        child_output,
        {{types::Value::integer(2)}, {types::Value::null(types::kInt64Type)}}
    );
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .subplans = {},
        .filter = subplan_reference(0U, types::kBoolType),
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(std::move(child_plan)),
            .strategy = planner::PlanSubPlanStrategy::kIn,
            .input_expression = input(0U, types::kInt64Type),
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    auto input_plan = values_plan(
        output,
        {{types::Value::integer(1)},
         {types::Value::integer(2)},
         {types::Value::null(types::kInt64Type)}}
    );
    auto input_state = ValuesState::create(input_plan);
    ASSERT_TRUE(input_state);
    ValuesFactory factory;
    auto state = SubPlanEvalState::create(plan, std::move(input_state).value(), factory);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows);
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->front(), (std::vector<types::Value>{types::Value::integer(2)}));
    ASSERT_TRUE(state.value()->rescan(context.value()));
    auto rescanned = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rescanned);
    EXPECT_EQ(rescanned.value(), rows.value());
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(SubPlanEvalStateTest, NotInProbesMaterializedMembershipValues) {
    const auto output = schema({{"id", types::kInt64Type}});
    const auto child_output = schema({{"value", types::kInt64Type}});
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .subplans = {},
        .filter = subplan_reference(0U, types::kBoolType),
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(
                values_plan(child_output, {{types::Value::integer(2)}})
            ),
            .strategy = planner::PlanSubPlanStrategy::kNotIn,
            .input_expression = input(0U, types::kInt64Type),
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    auto input_state = ValuesState::create(values_plan(
        output,
        {{types::Value::integer(1)},
         {types::Value::integer(2)},
         {types::Value::null(types::kInt64Type)},
         {types::Value::integer(3)}}
    ));
    ASSERT_TRUE(input_state);
    ValuesFactory factory;
    auto state = SubPlanEvalState::create(plan, std::move(input_state).value(), factory);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows) << rows.error().to_string();
    ASSERT_EQ(rows->size(), 2U);
    EXPECT_EQ(rows->at(0U).front(), types::Value::integer(1));
    EXPECT_EQ(rows->at(1U).front(), types::Value::integer(3));
}

TEST(SubPlanEvalStateTest, EmptyMembershipWithNullTestIsFalse) {
    const auto output = schema({{"id", types::kInt64Type}});
    const auto child_output = schema({{"value", types::kInt64Type}});
    auto child_plan = values_plan(child_output, {});
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .subplans = {},
        .filter = subplan_reference(0U, types::kBoolType),
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(std::move(child_plan)),
            .strategy = planner::PlanSubPlanStrategy::kIn,
            .input_expression = input(0U, types::kInt64Type),
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    auto input_state =
        ValuesState::create(values_plan(output, {{types::Value::null(types::kInt64Type)}}));
    ASSERT_TRUE(input_state);
    ValuesFactory factory;
    auto state = SubPlanEvalState::create(plan, std::move(input_state).value(), factory);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto next = state.value()->next(context.value());
    ASSERT_TRUE(next) << next.error().to_string();
    EXPECT_FALSE(next.value());
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(SubPlanEvalStateTest, NonEmptyMembershipWithNullTestIsUnknown) {
    const auto output = schema({{"id", types::kInt64Type}});
    const auto child_output = schema({{"value", types::kInt64Type}});
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .subplans = {},
        .filter = is_null(subplan_reference(0U, types::kBoolType)),
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(
                values_plan(child_output, {{types::Value::integer(7)}})
            ),
            .strategy = planner::PlanSubPlanStrategy::kIn,
            .input_expression = input(0U, types::kInt64Type),
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    auto input_state =
        ValuesState::create(values_plan(output, {{types::Value::null(types::kInt64Type)}}));
    ASSERT_TRUE(input_state);
    ValuesFactory factory;
    auto state = SubPlanEvalState::create(plan, std::move(input_state).value(), factory);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows) << rows.error().to_string();
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_TRUE(rows->front().front().is_null());
}

TEST(SubPlanEvalStateTest, ExistsAndNotExistsUseFirstChildRow) {
    const auto output = schema({{"id", types::kInt64Type}});
    const auto child_output = schema({{"value", types::kInt64Type}});
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .subplans = {},
        .filter = binary(
            types::BinaryOperator::kEqual,
            types::kBoolType,
            subplan_reference(0U, types::kBoolType),
            subplan_reference(1U, types::kBoolType)
        ),
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(
                values_plan(child_output, {{types::Value::integer(7)}})
            ),
            .strategy = planner::PlanSubPlanStrategy::kExists,
            .input_expression = nullptr,
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(values_plan(child_output, {})),
            .strategy = planner::PlanSubPlanStrategy::kNotExists,
            .input_expression = nullptr,
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    auto input_state = ValuesState::create(values_plan(output, {{types::Value::integer(1)}}));
    ASSERT_TRUE(input_state);
    ValuesFactory factory;
    auto state = SubPlanEvalState::create(plan, std::move(input_state).value(), factory);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows);
    EXPECT_EQ(
        rows.value(),
        (std::vector<std::vector<types::Value>>{
            {types::Value::integer(1)},
        })
    );
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(SubPlanEvalStateTest, EmptyScalarProducesTypedNullForIsNull) {
    const auto output = schema({{"id", types::kInt64Type}});
    const auto child_output = schema({{"value", types::kInt64Type}});
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .subplans = {},
        .filter = is_null(subplan_reference(0U, types::kInt64Type)),
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(values_plan(child_output, {})),
            .strategy = planner::PlanSubPlanStrategy::kScalar,
            .input_expression = nullptr,
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    auto input_state = ValuesState::create(values_plan(output, {{types::Value::integer(1)}}));
    ASSERT_TRUE(input_state);
    ValuesFactory factory;
    auto state = SubPlanEvalState::create(plan, std::move(input_state).value(), factory);
    ASSERT_TRUE(state) << state.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto rows = read_rows(*state.value(), context.value());
    ASSERT_TRUE(rows);
    ASSERT_EQ(rows->size(), 1U);
    EXPECT_EQ(rows->front(), (std::vector<types::Value>{types::Value::integer(1)}));
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(SubPlanEvalStateTest, RejectsScalarSubplanWithMultipleRows) {
    const auto output = schema({{"id", types::kInt64Type}});
    const auto child_output = schema({{"value", types::kInt64Type}});
    auto child_plan =
        values_plan(child_output, {{types::Value::integer(1)}, {types::Value::integer(2)}});
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .subplans = {},
        .filter = nullptr,
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(std::move(child_plan)),
            .strategy = planner::PlanSubPlanStrategy::kScalar,
            .input_expression = nullptr,
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    auto input_state = ValuesState::create(values_plan(output, {{types::Value::integer(0)}}));
    ASSERT_TRUE(input_state);
    ValuesFactory factory;
    auto state = SubPlanEvalState::create(plan, std::move(input_state).value(), factory);
    ASSERT_TRUE(state);
    auto context = empty_context();
    ASSERT_TRUE(context);
    ASSERT_TRUE(state.value()->open(context.value()));
    auto next = state.value()->next(context.value());
    ASSERT_FALSE(next);
    EXPECT_EQ(next.error().code(), base::ErrorCode::kExecution);
    ASSERT_TRUE(state.value()->close(context.value()));
}

TEST(SubPlanEvalStateTest, RejectsScalarSubplanWithMultipleColumnsAtConstruction) {
    const auto output = schema({{"id", types::kInt64Type}});
    const auto child_output = schema({{"left", types::kInt64Type}, {"right", types::kInt64Type}});
    planner::SubPlanEvalPlan plan{
        .metadata = planner::PlanOperatorMetadata{.output = output},
        .input = nullptr,
        .subplans = {},
        .filter = nullptr,
    };
    plan.subplans.push_back(
        planner::PlanSubPlan{
            .plan = std::make_unique<planner::Plan>(values_plan(child_output, {})),
            .strategy = planner::PlanSubPlanStrategy::kScalar,
            .input_expression = nullptr,
            .arguments = {},
            .parameters = {},
            .uses_inherited_parameters = false,
        }
    );
    auto input_state = ValuesState::create(values_plan(output, {}));
    ASSERT_TRUE(input_state);
    ValuesFactory factory;

    auto state = SubPlanEvalState::create(plan, std::move(input_state).value(), factory);

    ASSERT_FALSE(state);
    EXPECT_EQ(state.error().code(), base::ErrorCode::kPlan);
}

}  // namespace
}  // namespace db::execution
