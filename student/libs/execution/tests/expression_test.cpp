#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/execution/exec_context.hpp"
#include "db/execution/expression.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/plan.hpp"
#include "db/types/coercion.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::execution {
namespace {

using planner::PlanExpression;
using planner::PlanExpressionPtr;

auto literal(types::Value value) -> PlanExpressionPtr {
    const types::DataType type = value.data_type();
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        type,
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

auto boolean(const types::BooleanOperator operation, std::vector<PlanExpressionPtr> operands)
    -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kBoolType,
        planner::PlanBooleanExpression{
            .op = operation,
            .operands = std::move(operands),
        }
    );
}

auto operands(PlanExpressionPtr first, PlanExpressionPtr second) -> std::vector<PlanExpressionPtr> {
    std::vector<PlanExpressionPtr> result;
    result.push_back(std::move(first));
    result.push_back(std::move(second));
    return result;
}

auto values(PlanExpressionPtr first, PlanExpressionPtr second) -> std::vector<PlanExpressionPtr> {
    return operands(std::move(first), std::move(second));
}

auto unary(
    const types::UnaryOperator operation,
    const types::DataType type,
    PlanExpressionPtr operand
) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        type,
        planner::PlanUnaryExpression{
            .op = operation,
            .operand = std::move(operand),
        }
    );
}

auto is_null(const bool negated, PlanExpressionPtr operand) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kBoolType,
        planner::PlanIsNullExpression{
            .negated = negated,
            .operand = std::move(operand),
        }
    );
}

auto in_list(const bool negated, PlanExpressionPtr operand, std::vector<PlanExpressionPtr> values)
    -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kBoolType,
        planner::PlanInListExpression{
            .negated = negated,
            .operand = std::move(operand),
            .values = std::move(values),
        }
    );
}

auto between(
    const bool negated,
    PlanExpressionPtr operand,
    PlanExpressionPtr lower,
    PlanExpressionPtr upper
) -> PlanExpressionPtr {
    return std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kBoolType,
        planner::PlanBetweenExpression{
            .negated = negated,
            .operand = std::move(operand),
            .lower = std::move(lower),
            .upper = std::move(upper),
        }
    );
}

auto make_tuple(std::vector<types::Value> values) -> base::Result<Tuple> {
    std::vector<TupleColumn> columns;
    columns.reserve(values.size());
    for (std::size_t index = 0U; index < values.size(); ++index) {
        columns.push_back(
            TupleColumn{
                .name = "column_" + std::to_string(index),
                .data_type = values[index].data_type(),
            }
        );
    }
    auto descriptor = TupleDescriptor::create(std::move(columns));
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    return Tuple::create(std::move(descriptor).value(), std::move(values));
}

auto make_empty_tuple() -> base::Result<Tuple> {
    std::vector<types::Value> values;
    return make_tuple(std::move(values));
}

auto empty_context() -> base::Result<ExecContext> {
    const std::vector<planner::ExternalParameterSlot> external_slots;
    auto namespace_result = planner::ParameterNamespace::from_external_slots(external_slots);
    if (!namespace_result) {
        return base::fail(std::move(namespace_result).error());
    }
    return ExecContext::create(namespace_result.value(), {});
}

template <typename Result>
void expect_unavailable(const Result& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inactive(ExpressionProgram& program, const Tuple& tuple, const ExecContext& context) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(program.valid());

    expect_unavailable(program.clone());
    expect_unavailable(program.evaluate(tuple, context));

    const std::span<const types::Value> subplan_values;
    expect_unavailable(program.evaluate(tuple, context, subplan_values));
    expect_unavailable(program.evaluate_qualification(tuple, context));
    expect_unavailable(program.evaluate_qualification(tuple, context, subplan_values));
}

TEST(ExpressionProgramTest, EvaluatesInputArithmeticAndCanBeReused) {
    auto expression = binary(
        types::BinaryOperator::kAdd,
        types::kInt64Type,
        input(0U, types::kInt64Type),
        literal(types::Value::integer(3))
    );
    auto program = ExpressionProgram::compile(*expression);
    ASSERT_TRUE(program) << program.error().to_string();
    auto retained = program->clone();
    ASSERT_TRUE(retained) << retained.error().to_string();
    auto tuple = make_tuple({types::Value::integer(4)});
    ASSERT_TRUE(tuple) << tuple.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();

    const auto first = program->evaluate(tuple.value(), context.value());
    ASSERT_TRUE(first) << first.error().to_string();
    EXPECT_EQ(first.value(), types::Value::integer(7));
    const auto retained_value = retained->evaluate(tuple.value(), context.value());
    ASSERT_TRUE(retained_value) << retained_value.error().to_string();
    EXPECT_EQ(retained_value.value(), types::Value::integer(7));

    auto second_tuple = make_tuple({types::Value::integer(11)});
    ASSERT_TRUE(second_tuple) << second_tuple.error().to_string();
    const auto second = program->evaluate(second_tuple.value(), context.value());
    ASSERT_TRUE(second) << second.error().to_string();
    EXPECT_EQ(second.value(), types::Value::integer(14));
}

TEST(ExpressionProgramTest, BindsInputTypesOncePerTupleDescriptorAndRejectsAMismatch) {
    auto expression = binary(
        types::BinaryOperator::kAdd,
        types::kInt64Type,
        input(0U, types::kInt64Type),
        literal(types::Value::integer(3))
    );
    auto program = ExpressionProgram::compile(*expression);
    ASSERT_TRUE(program) << program.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();

    auto integers = TupleDescriptor::create({TupleColumn{
        .name = "value",
        .data_type = types::kInt64Type,
    }});
    ASSERT_TRUE(integers) << integers.error().to_string();
    auto first = Tuple::create(integers.value(), {types::Value::integer(4)});
    auto second = Tuple::create(integers.value(), {types::Value::integer(11)});
    ASSERT_TRUE(first) << first.error().to_string();
    ASSERT_TRUE(second) << second.error().to_string();
    const auto first_value = program->evaluate(first.value(), context.value());
    ASSERT_TRUE(first_value) << first_value.error().to_string();
    EXPECT_EQ(first_value.value(), types::Value::integer(7));
    const auto second_value = program->evaluate(second.value(), context.value());
    ASSERT_TRUE(second_value) << second_value.error().to_string();
    EXPECT_EQ(second_value.value(), types::Value::integer(14));

    auto floats = TupleDescriptor::create({TupleColumn{
        .name = "value",
        .data_type = types::kFloat32Type,
    }});
    ASSERT_TRUE(floats) << floats.error().to_string();
    auto mismatched = Tuple::create(floats.value(), {types::Value::floating(4.0F)});
    ASSERT_TRUE(mismatched) << mismatched.error().to_string();
    auto rejected = program->evaluate(mismatched.value(), context.value());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kExecution);

    const auto rebound = program->evaluate(first.value(), context.value());
    ASSERT_TRUE(rebound) << rebound.error().to_string();
    EXPECT_EQ(rebound.value(), types::Value::integer(7));
}

TEST(ExpressionProgramTest, RejectsAMovedFromInputTuple) {
    auto expression = input(0U, types::kInt64Type);
    auto program = ExpressionProgram::compile(*expression);
    ASSERT_TRUE(program) << program.error().to_string();
    auto created = make_tuple({types::Value::integer(4)});
    ASSERT_TRUE(created) << created.error().to_string();
    Tuple source = std::move(created).value();
    Tuple moved{std::move(source)};
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();

    // Calling a public operation on the documented C++ moved-from state must
    // fail structurally rather than dereference the transferred descriptor.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    const auto rejected = program->evaluate(source, context.value());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnavailable);

    const auto accepted = program->evaluate(moved, context.value());
    ASSERT_TRUE(accepted) << accepted.error().to_string();
    EXPECT_EQ(accepted.value(), types::Value::integer(4));
}

TEST(ExpressionProgramTest, MoveTransfersTheProgramAndLeavesTheSourceInert) {
    auto expression = literal(types::Value::integer(7));
    auto compiled = ExpressionProgram::compile(*expression);
    ASSERT_TRUE(compiled) << compiled.error().to_string();
    ExpressionProgram source = std::move(compiled).value();
    auto tuple = make_empty_tuple();
    ASSERT_TRUE(tuple) << tuple.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();

    ExpressionProgram moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive(source, tuple.value(), context.value());

    auto assigned_result = ExpressionProgram::compile(*expression);
    ASSERT_TRUE(assigned_result) << assigned_result.error().to_string();
    ExpressionProgram assigned = std::move(assigned_result).value();
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive(moved, tuple.value(), context.value());

    const auto value = assigned.evaluate(tuple.value(), context.value());
    ASSERT_TRUE(value) << value.error().to_string();
    EXPECT_EQ(value.value(), types::Value::integer(7));
}

TEST(ExpressionProgramTest, PropagatesNullAndUsesThreeValuedBooleanLogic) {
    auto false_and_unknown = boolean(
        types::BooleanOperator::kAnd,
        operands(
            literal(types::Value::boolean(false)),
            literal(types::Value::null(types::kBoolType))
        )
    );
    auto unknown_or_true = boolean(
        types::BooleanOperator::kOr,
        operands(
            literal(types::Value::null(types::kBoolType)),
            literal(types::Value::boolean(true))
        )
    );
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();
    auto tuple = make_empty_tuple();
    ASSERT_TRUE(tuple) << tuple.error().to_string();

    auto first_program = ExpressionProgram::compile(*false_and_unknown);
    ASSERT_TRUE(first_program) << first_program.error().to_string();
    const auto first = first_program->evaluate(tuple.value(), context.value());
    ASSERT_TRUE(first) << first.error().to_string();
    EXPECT_EQ(first.value(), types::Value::boolean(false));

    auto second_program = ExpressionProgram::compile(*unknown_or_true);
    ASSERT_TRUE(second_program) << second_program.error().to_string();
    const auto second = second_program->evaluate(tuple.value(), context.value());
    ASSERT_TRUE(second) << second.error().to_string();
    EXPECT_EQ(second.value(), types::Value::boolean(true));

    auto null_not = unary(
        types::UnaryOperator::kNot,
        types::kBoolType,
        literal(types::Value::null(types::kBoolType))
    );
    auto not_program = ExpressionProgram::compile(*null_not);
    ASSERT_TRUE(not_program) << not_program.error().to_string();
    const auto not_value = not_program->evaluate(tuple.value(), context.value());
    ASSERT_TRUE(not_value) << not_value.error().to_string();
    EXPECT_EQ(not_value.value(), types::Value::null(types::kBoolType));
}

TEST(ExpressionProgramTest, RejectsLogicalOperatorsEncodedAsScalarBinaryNodes) {
    auto expression = binary(
        types::BinaryOperator::kAnd,
        types::kInt64Type,
        literal(types::Value::integer(1)),
        literal(types::Value::integer(0))
    );

    const auto program = ExpressionProgram::compile(*expression);
    ASSERT_FALSE(program);
    EXPECT_EQ(program.error().code(), base::ErrorCode::kPlan);
}

TEST(ExpressionProgramTest, InBetweenAndIsNullPreserveUnknownAndNegation) {
    auto tuple = make_tuple({types::Value::integer(3)});
    ASSERT_TRUE(tuple) << tuple.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();

    auto in_expression = in_list(
        false,
        input(0U, types::kInt64Type),
        values(literal(types::Value::integer(1)), literal(types::Value::null(types::kInt64Type)))
    );
    auto in_program = ExpressionProgram::compile(*in_expression);
    ASSERT_TRUE(in_program) << in_program.error().to_string();
    const auto in_value = in_program->evaluate(tuple.value(), context.value());
    ASSERT_TRUE(in_value) << in_value.error().to_string();
    EXPECT_EQ(in_value.value(), types::Value::null(types::kBoolType));

    auto between_expression = between(
        true,
        input(0U, types::kInt64Type),
        literal(types::Value::integer(1)),
        literal(types::Value::integer(2))
    );
    auto between_program = ExpressionProgram::compile(*between_expression);
    ASSERT_TRUE(between_program) << between_program.error().to_string();
    const auto between_value = between_program->evaluate(tuple.value(), context.value());
    ASSERT_TRUE(between_value) << between_value.error().to_string();
    EXPECT_EQ(between_value.value(), types::Value::boolean(true));

    auto is_null_expression = is_null(false, literal(types::Value::null(types::kInt64Type)));
    auto is_null_program = ExpressionProgram::compile(*is_null_expression);
    ASSERT_TRUE(is_null_program) << is_null_program.error().to_string();
    const auto is_null_value = is_null_program->evaluate(tuple.value(), context.value());
    ASSERT_TRUE(is_null_value) << is_null_value.error().to_string();
    EXPECT_EQ(is_null_value.value(), types::Value::boolean(true));
}

TEST(ExpressionProgramTest, ReadsTypedExternalParametersAndAppliesCasts) {
    const std::vector external_slots{
        planner::ExternalParameterSlot{
            .slot = planner::ExternalParameterNumber{1U},
            .data_type = types::kInt64Type,
        },
    };
    auto namespace_result = planner::ParameterNamespace::from_external_slots(external_slots);
    ASSERT_TRUE(namespace_result) << namespace_result.error().to_string();
    auto reference = namespace_result->external_reference(planner::ExternalParameterNumber{1U});
    ASSERT_TRUE(reference) << reference.error().to_string();
    const std::vector external_values{types::Value::integer(9)};
    auto context = ExecContext::create(namespace_result.value(), external_values);
    ASSERT_TRUE(context) << context.error().to_string();

    auto expression = binary(
        types::BinaryOperator::kAdd,
        types::kFloat32Type,
        std::make_unique<PlanExpression>(
            planner::PlanSourceSpan{},
            types::kFloat32Type,
            planner::PlanCastExpression{
                .operand = std::make_unique<
                    PlanExpression>(planner::PlanSourceSpan{}, types::kInt64Type, *reference),
                .target_type = types::kFloat32Type,
                .path = *types::find_coercion_path(
                    types::kInt64Type,
                    types::kFloat32Type,
                    types::CoercionContext::kImplicit
                ),
                .form = planner::PlanCastForm::kImplicit,
            }
        ),
        literal(types::Value::floating(0.5F))
    );
    auto program = ExpressionProgram::compile(*expression);
    ASSERT_TRUE(program) << program.error().to_string();
    auto tuple = make_empty_tuple();
    ASSERT_TRUE(tuple) << tuple.error().to_string();
    const auto value = program->evaluate(tuple.value(), context.value());
    ASSERT_TRUE(value) << value.error().to_string();
    ASSERT_NE(value->as_float(), nullptr);
    EXPECT_FLOAT_EQ(*value->as_float(), 9.5F);

    const std::vector boolean_slots{
        planner::ExternalParameterSlot{
            .slot = planner::ExternalParameterNumber{1U},
            .data_type = types::kBoolType,
        },
    };
    auto boolean_namespace = planner::ParameterNamespace::from_external_slots(boolean_slots);
    ASSERT_TRUE(boolean_namespace) << boolean_namespace.error().to_string();
    const std::vector boolean_values{types::Value::boolean(true)};
    auto mismatched_context = ExecContext::create(boolean_namespace.value(), boolean_values);
    ASSERT_TRUE(mismatched_context) << mismatched_context.error().to_string();
    auto rejected = program->evaluate(tuple.value(), mismatched_context.value());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kExecution);

    const auto rebound = program->evaluate(tuple.value(), context.value());
    ASSERT_TRUE(rebound) << rebound.error().to_string();
    ASSERT_NE(rebound->as_float(), nullptr);
    EXPECT_FLOAT_EQ(*rebound->as_float(), 9.5F);
}

TEST(ExpressionProgramTest, ReportsRuntimeErrorsAndQualificationSemantics) {
    auto tuple = make_tuple({types::Value::integer(1)});
    ASSERT_TRUE(tuple) << tuple.error().to_string();
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();

    auto division = binary(
        types::BinaryOperator::kDivide,
        types::kInt64Type,
        input(0U, types::kInt64Type),
        literal(types::Value::integer(0))
    );
    auto division_program = ExpressionProgram::compile(*division);
    ASSERT_TRUE(division_program) << division_program.error().to_string();
    const auto division_value = division_program->evaluate(tuple.value(), context.value());
    ASSERT_FALSE(division_value);
    EXPECT_EQ(division_value.error().code(), base::ErrorCode::kExecution);

    auto qualification = binary(
        types::BinaryOperator::kEqual,
        types::kBoolType,
        input(0U, types::kInt64Type),
        literal(types::Value::integer(1))
    );
    auto qualification_program = ExpressionProgram::compile(*qualification);
    ASSERT_TRUE(qualification_program) << qualification_program.error().to_string();
    const auto qualified =
        qualification_program->evaluate_qualification(tuple.value(), context.value());
    ASSERT_TRUE(qualified) << qualified.error().to_string();
    EXPECT_TRUE(qualified.value());

    auto non_boolean = literal(types::Value::integer(1));
    auto non_boolean_program = ExpressionProgram::compile(*non_boolean);
    ASSERT_TRUE(non_boolean_program) << non_boolean_program.error().to_string();
    const auto rejected =
        non_boolean_program->evaluate_qualification(tuple.value(), context.value());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kExecution);
}

TEST(ExpressionProgramTest, DirectQualificationsPreserveNullLeafAndParameterSemantics) {
    auto context = empty_context();
    ASSERT_TRUE(context) << context.error().to_string();

    auto comparison = binary(
        types::BinaryOperator::kGreaterEqual,
        types::kBoolType,
        input(0U, types::kInt64Type),
        literal(types::Value::integer(4))
    );
    auto comparison_program = ExpressionProgram::compile(*comparison);
    ASSERT_TRUE(comparison_program) << comparison_program.error().to_string();
    auto greater = make_tuple({types::Value::integer(7)});
    auto lesser = make_tuple({types::Value::integer(3)});
    auto null_integer = make_tuple({types::Value::null(types::kInt64Type)});
    ASSERT_TRUE(greater) << greater.error().to_string();
    ASSERT_TRUE(lesser) << lesser.error().to_string();
    ASSERT_TRUE(null_integer) << null_integer.error().to_string();
    auto qualified = comparison_program->evaluate_qualification(greater.value(), context.value());
    ASSERT_TRUE(qualified) << qualified.error().to_string();
    EXPECT_TRUE(qualified.value());
    qualified = comparison_program->evaluate_qualification(lesser.value(), context.value());
    ASSERT_TRUE(qualified) << qualified.error().to_string();
    EXPECT_FALSE(qualified.value());
    qualified = comparison_program->evaluate_qualification(null_integer.value(), context.value());
    ASSERT_TRUE(qualified) << qualified.error().to_string();
    EXPECT_FALSE(qualified.value());

    auto null_test = is_null(true, input(0U, types::kInt64Type));
    auto null_program = ExpressionProgram::compile(*null_test);
    ASSERT_TRUE(null_program) << null_program.error().to_string();
    qualified = null_program->evaluate_qualification(greater.value(), context.value());
    ASSERT_TRUE(qualified) << qualified.error().to_string();
    EXPECT_TRUE(qualified.value());
    qualified = null_program->evaluate_qualification(null_integer.value(), context.value());
    ASSERT_TRUE(qualified) << qualified.error().to_string();
    EXPECT_FALSE(qualified.value());

    auto boolean_program = ExpressionProgram::compile(*input(0U, types::kBoolType));
    ASSERT_TRUE(boolean_program) << boolean_program.error().to_string();
    auto true_value = make_tuple({types::Value::boolean(true)});
    auto null_boolean = make_tuple({types::Value::null(types::kBoolType)});
    ASSERT_TRUE(true_value) << true_value.error().to_string();
    ASSERT_TRUE(null_boolean) << null_boolean.error().to_string();
    qualified = boolean_program->evaluate_qualification(true_value.value(), context.value());
    ASSERT_TRUE(qualified) << qualified.error().to_string();
    EXPECT_TRUE(qualified.value());
    qualified = boolean_program->evaluate_qualification(null_boolean.value(), context.value());
    ASSERT_TRUE(qualified) << qualified.error().to_string();
    EXPECT_FALSE(qualified.value());

    const std::vector external_slots{
        planner::ExternalParameterSlot{
            .slot = planner::ExternalParameterNumber{1U},
            .data_type = types::kInt64Type,
        },
    };
    auto parameter_namespace = planner::ParameterNamespace::from_external_slots(external_slots);
    ASSERT_TRUE(parameter_namespace) << parameter_namespace.error().to_string();
    auto reference = parameter_namespace->external_reference(planner::ExternalParameterNumber{1U});
    ASSERT_TRUE(reference) << reference.error().to_string();
    auto parameter_expression = binary(
        types::BinaryOperator::kEqual,
        types::kBoolType,
        input(0U, types::kInt64Type),
        std::make_unique<PlanExpression>(
            planner::PlanSourceSpan{},
            types::kInt64Type,
            reference.value()
        )
    );
    auto parameter_program = ExpressionProgram::compile(*parameter_expression);
    ASSERT_TRUE(parameter_program) << parameter_program.error().to_string();
    const std::vector parameter_values{types::Value::integer(7)};
    auto parameter_context = ExecContext::create(parameter_namespace.value(), parameter_values);
    ASSERT_TRUE(parameter_context) << parameter_context.error().to_string();
    qualified =
        parameter_program->evaluate_qualification(greater.value(), parameter_context.value());
    ASSERT_TRUE(qualified) << qualified.error().to_string();
    EXPECT_TRUE(qualified.value());
}

TEST(ExpressionProgramTest, RejectsUnsupportedLaterStageNodesAtCompileTime) {
    auto aggregate = std::make_unique<PlanExpression>(
        planner::PlanSourceSpan{},
        types::kInt64Type,
        planner::PlanAggregateExpression{}
    );
    const auto program = ExpressionProgram::compile(*aggregate);
    ASSERT_FALSE(program);
    EXPECT_EQ(program.error().code(), base::ErrorCode::kUnsupported);
}

}  // namespace
}  // namespace db::execution
