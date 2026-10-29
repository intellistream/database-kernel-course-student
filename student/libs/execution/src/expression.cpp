#include "db/execution/expression.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/checked.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/execution/exec_context.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/plan.hpp"
#include "db/types/coercion.hpp"
#include "db/types/type_registry.hpp"

namespace db::execution {

namespace {

using planner::PlanExpression;
using planner::PlanExpressionPtr;

[[maybe_unused]] auto expression_error(
    const base::ErrorCode code,
    std::string message,
    const std::string_view operation = "evaluate"
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "execution.expression",
        std::string{operation}
    );
}

// LAB-S02.Q06:R01-BEGIN
[[maybe_unused]] auto null_result([[maybe_unused]] const types::DataType type) -> types::Value {
    base::lab::missing_algorithm("S02.Q06:R01");
}

// LAB-S02.Q06:R01-END

// LAB-S02.Q06:R02-BEGIN
[[maybe_unused]] auto is_boolean_type([[maybe_unused]] const types::DataType type) noexcept
    -> bool {
    base::lab::missing_algorithm("S02.Q06:R02");
}

// LAB-S02.Q06:R02-END

// LAB-S02.Q06:R03-BEGIN
[[maybe_unused]] auto is_numeric_type([[maybe_unused]] const types::DataType type) noexcept
    -> bool {
    base::lab::missing_algorithm("S02.Q06:R03");
}

// LAB-S02.Q06:R03-END

// LAB-S02.Q06:R04-BEGIN
[[maybe_unused]] auto is_float_modulo(
    [[maybe_unused]] const types::BinaryOperator op,
    [[maybe_unused]] const types::DataType type
) noexcept -> bool {
    base::lab::missing_algorithm("S02.Q06:R04");
}

// LAB-S02.Q06:R04-END

// LAB-S02.Q06:R05-BEGIN
[[maybe_unused]] auto invalid_plan_expression([[maybe_unused]] std::string message)
    -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R05: validate expression plan",
        "lab"
    ));
}

// LAB-S02.Q06:R05-END

// LAB-S02.Q06:R06-BEGIN
[[maybe_unused]] auto comparison_truth(
    [[maybe_unused]] const types::Value& left,
    [[maybe_unused]] const types::Value& right,
    [[maybe_unused]] const types::BinaryOperator op
) -> base::Result<types::SqlTruth> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R06: compare expression values",
        "lab"
    ));
}

// LAB-S02.Q06:R06-END

// LAB-S02.Q06:R07-BEGIN
[[maybe_unused]] auto truth_value(
    [[maybe_unused]] const types::SqlTruth truth,
    [[maybe_unused]] const types::DataType result_type
) -> base::Result<types::Value> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R07: convert SQL truth value",
        "lab"
    ));
}

// LAB-S02.Q06:R07-END

// LAB-S02.Q06:R08-BEGIN
[[maybe_unused]] auto value_truth([[maybe_unused]] const types::Value& value)
    -> base::Result<types::SqlTruth> {
    return base::fail(
        expression_error(base::ErrorCode::kUnsupported, "S02.Q06:R08: read SQL truth value", "lab")
    );
}

// LAB-S02.Q06:R08-END

// LAB-S02.Q06:R09-BEGIN
[[maybe_unused]] auto evaluate_integer_arithmetic(
    [[maybe_unused]] const types::BinaryOperator op,
    [[maybe_unused]] const std::int64_t left,
    [[maybe_unused]] const std::int64_t right
) -> base::Result<types::Value> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R09: evaluate integer arithmetic",
        "lab"
    ));
}

// LAB-S02.Q06:R09-END

// LAB-S02.Q06:R10-BEGIN
[[maybe_unused]] auto evaluate_float_arithmetic(
    [[maybe_unused]] const types::BinaryOperator op,
    [[maybe_unused]] const float left,
    [[maybe_unused]] const float right
) -> base::Result<types::Value> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R10: evaluate float arithmetic",
        "lab"
    ));
}

// LAB-S02.Q06:R10-END

// LAB-S02.Q06:R11-BEGIN
[[maybe_unused]] auto evaluate_arithmetic(
    [[maybe_unused]] const types::BinaryOperator op,
    [[maybe_unused]] const types::Value& left,
    [[maybe_unused]] const types::Value& right,
    [[maybe_unused]] const types::DataType result_type
) -> base::Result<types::Value> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R11: evaluate arithmetic expression",
        "lab"
    ));
}

// LAB-S02.Q06:R11-END

// LAB-S02.Q06:R12-BEGIN
[[maybe_unused]] auto evaluate_unary(
    [[maybe_unused]] const types::UnaryOperator op,
    [[maybe_unused]] const types::Value& value,
    [[maybe_unused]] const types::DataType result_type
) -> base::Result<types::Value> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R12: evaluate unary expression",
        "lab"
    ));
}

// LAB-S02.Q06:R12-END

// LAB-S02.Q06:R13-BEGIN
[[maybe_unused]] auto evaluate_binary(
    [[maybe_unused]] const types::BinaryOperator op,
    [[maybe_unused]] const types::Value& left,
    [[maybe_unused]] const types::Value& right,
    [[maybe_unused]] const types::DataType result_type
) -> base::Result<types::Value> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R13: evaluate binary expression",
        "lab"
    ));
}

// LAB-S02.Q06:R13-END

}  // namespace

struct ExpressionProgram::Impl final {
    enum class InstructionKind : std::uint8_t {
        kLiteral,
        kInputColumn,
        kParameter,
        kUnary,
        kBinary,
        kBoolean,
        kIsNull,
        kInList,
        kBetween,
        kCast,
        kSubplan,
    };

    enum class QualificationKind : std::uint8_t {
        kGeneric,
        kLeaf,
        kComparison,
        kIsNull,
    };

    struct Instruction final {
        InstructionKind kind{InstructionKind::kLiteral};
        types::DataType result_type{types::kUnknownType};
        types::Value literal{types::Value::null(types::kUnknownType)};
        planner::PlanColumnIndex input_column{};
        planner::PlanParameterReference parameter{};
        planner::PlanSubPlanIndex subplan_index{};
        types::UnaryOperator unary{types::UnaryOperator::kPlus};
        types::BinaryOperator binary{types::BinaryOperator::kEqual};
        types::BooleanOperator boolean{types::BooleanOperator::kAnd};
        bool negated{false};
        std::size_t operand_count{0U};
        types::DataType cast_target{types::kUnknownType};
        types::CoercionPath cast_path{};
    };

    std::vector<Instruction> instructions;
    types::DataType result_type{types::kUnknownType};
    mutable TupleDescriptorHandle input_descriptor;
    mutable ParameterDescriptorHandle parameter_descriptor;
    QualificationKind qualification_kind{QualificationKind::kGeneric};

    using Stack = std::vector<types::Value>;
    mutable Stack scratch;

    auto compile(const PlanExpression& expression) -> base::Status;
    auto compile_child(const PlanExpressionPtr& child, std::string_view name) -> base::Status;
    void append(Instruction instruction);

    auto compile_node(types::DataType expected, const planner::PlanLiteralExpression& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanInputColumnReference& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanParameterReference& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanUnaryExpression& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanBinaryExpression& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanBooleanExpression& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanIsNullExpression& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanInListExpression& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanBetweenExpression& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanCastExpression& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanAggregateExpression& node)
        -> base::Status;
    auto compile_node(types::DataType expected, const planner::PlanSubPlanReference& node)
        -> base::Status;

    auto evaluate(
        const Tuple& tuple,
        const ExecContext& context,
        std::span<const types::Value> subplan_values
    ) const -> base::Result<types::Value>;
    void select_qualification_kind() noexcept;
    auto leaf_reference(
        const Instruction& instruction,
        const Tuple& tuple,
        const ExecContext& context,
        std::span<const types::Value> subplan_values
    ) const -> base::Result<std::reference_wrapper<const types::Value>>;
    auto evaluate_qualification(
        const Tuple& tuple,
        const ExecContext& context,
        std::span<const types::Value> subplan_values
    ) const -> base::Result<bool>;
    auto bind_input_descriptor(const Tuple& tuple) const -> base::Status;
    auto bind_parameter_descriptor(const ExecContext& context) const -> base::Status;
    auto execute_instruction(
        const Instruction& instruction,
        const Tuple& tuple,
        const ExecContext& context,
        Stack& stack,
        std::span<const types::Value> subplan_values
    ) const -> base::Status;
    static auto is_leaf(InstructionKind kind) noexcept -> bool;
    auto evaluate_leaf(
        const Instruction& instruction,
        const Tuple& tuple,
        const ExecContext& context,
        std::span<const types::Value> subplan_values
    ) const -> base::Result<types::Value>;
    auto reduce_unary(const Instruction& instruction, Stack& stack) const -> base::Status;
    auto reduce_binary(const Instruction& instruction, Stack& stack) const -> base::Status;
    auto boolean_truth(const Instruction& instruction, const Stack& stack) const
        -> base::Result<types::SqlTruth>;
    auto reduce_boolean(const Instruction& instruction, Stack& stack) const -> base::Status;
    auto in_list_truth(const Instruction& instruction, const Stack& stack) const
        -> base::Result<types::SqlTruth>;
    auto reduce_in_list(const Instruction& instruction, Stack& stack) const -> base::Status;
    auto reduce_between(const Instruction& instruction, Stack& stack) const -> base::Status;
    auto reduce_cast(const Instruction& instruction, Stack& stack) const -> base::Status;
};

// LAB-S02.Q06:R14-BEGIN
[[maybe_unused]] auto
ExpressionProgram::Impl::compile([[maybe_unused]] const PlanExpression& expression)
    -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R14: compile expression program",
        "lab"
    ));
}

// LAB-S02.Q06:R14-END

// LAB-S02.Q06:R15-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_child(
    [[maybe_unused]] const PlanExpressionPtr& child,
    [[maybe_unused]] const std::string_view name
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R15: compile child expression",
        "lab"
    ));
}

// LAB-S02.Q06:R15-END

// LAB-S02.Q06:R16-BEGIN
[[maybe_unused]] void ExpressionProgram::Impl::append([[maybe_unused]] Instruction instruction) {
    base::lab::missing_algorithm("S02.Q06:R16");
}

// LAB-S02.Q06:R16-END

// LAB-S02.Q06:R17-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanLiteralExpression& node
) -> base::Status {
    return base::fail(
        expression_error(base::ErrorCode::kUnsupported, "S02.Q06:R17: compile literal", "lab")
    );
}

// LAB-S02.Q06:R17-END

// LAB-S02.Q06:R18-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanInputColumnReference& node
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R18: compile input reference",
        "lab"
    ));
}

// LAB-S02.Q06:R18-END

// LAB-S02.Q06:R19-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanParameterReference& node
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R19: compile parameter reference",
        "lab"
    ));
}

// LAB-S02.Q06:R19-END

// LAB-S02.Q06:R20-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanUnaryExpression& node
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R20: compile unary expression",
        "lab"
    ));
}

// LAB-S02.Q06:R20-END

// LAB-S02.Q06:R21-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanBinaryExpression& node
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R21: compile binary expression",
        "lab"
    ));
}

// LAB-S02.Q06:R21-END

// LAB-S02.Q06:R22-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanBooleanExpression& node
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R22: compile boolean expression",
        "lab"
    ));
}

// LAB-S02.Q06:R22-END

// LAB-S02.Q06:R23-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanIsNullExpression& node
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R23: compile IS NULL expression",
        "lab"
    ));
}

// LAB-S02.Q06:R23-END

// LAB-S02.Q06:R24-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanInListExpression& node
) -> base::Status {
    return base::fail(
        expression_error(base::ErrorCode::kUnsupported, "S02.Q06:R24: compile IN expression", "lab")
    );
}

// LAB-S02.Q06:R24-END

// LAB-S02.Q06:R25-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanBetweenExpression& node
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R25: compile BETWEEN expression",
        "lab"
    ));
}

// LAB-S02.Q06:R25-END

// LAB-S02.Q06:R26-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanCastExpression& node
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R26: compile cast expression",
        "lab"
    ));
}

// LAB-S02.Q06:R26-END

// LAB-S02.Q06:R27-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanAggregateExpression& node
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R27: compile aggregate expression",
        "lab"
    ));
}

// LAB-S02.Q06:R27-END

// LAB-S02.Q06:R28-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::compile_node(
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const planner::PlanSubPlanReference& node
) -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R28: compile subplan reference",
        "lab"
    ));
}

// LAB-S02.Q06:R28-END

// LAB-S02.Q06:R29-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::reduce_unary(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] Stack& stack
) const -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R29: reduce unary instruction",
        "lab"
    ));
}

// LAB-S02.Q06:R29-END

// LAB-S02.Q06:R30-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::reduce_binary(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] Stack& stack
) const -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R30: reduce binary instruction",
        "lab"
    ));
}

// LAB-S02.Q06:R30-END

// LAB-S02.Q06:R31-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::boolean_truth(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] const Stack& stack
) const -> base::Result<types::SqlTruth> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R31: evaluate boolean instruction",
        "lab"
    ));
}

// LAB-S02.Q06:R31-END

// LAB-S02.Q06:R32-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::reduce_boolean(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] Stack& stack
) const -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R32: reduce boolean instruction",
        "lab"
    ));
}

// LAB-S02.Q06:R32-END

// LAB-S02.Q06:R33-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::in_list_truth(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] const Stack& stack
) const -> base::Result<types::SqlTruth> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R33: evaluate IN instruction",
        "lab"
    ));
}

// LAB-S02.Q06:R33-END

// LAB-S02.Q06:R34-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::reduce_in_list(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] Stack& stack
) const -> base::Status {
    return base::fail(
        expression_error(base::ErrorCode::kUnsupported, "S02.Q06:R34: reduce IN instruction", "lab")
    );
}

// LAB-S02.Q06:R34-END

// LAB-S02.Q06:R35-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::reduce_between(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] Stack& stack
) const -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R35: reduce BETWEEN instruction",
        "lab"
    ));
}

// LAB-S02.Q06:R35-END

// LAB-S02.Q06:R36-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::reduce_cast(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] Stack& stack
) const -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R36: reduce cast instruction",
        "lab"
    ));
}

// LAB-S02.Q06:R36-END

// LAB-S02.Q06:R37-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::execute_instruction(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] const ExecContext& context,
    [[maybe_unused]] Stack& stack,
    [[maybe_unused]] const std::span<const types::Value> subplan_values
) const -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R37: execute expression instruction",
        "lab"
    ));
}

// LAB-S02.Q06:R37-END

// LAB-S02.Q06:R38-BEGIN
[[maybe_unused]] auto
ExpressionProgram::Impl::is_leaf([[maybe_unused]] const InstructionKind kind) noexcept -> bool {
    base::lab::missing_algorithm("S02.Q06:R38");
}

// LAB-S02.Q06:R38-END

// LAB-S02.Q06:R39-BEGIN
[[maybe_unused]] void ExpressionProgram::Impl::select_qualification_kind() noexcept {
    base::lab::missing_algorithm("S02.Q06:R39");
}

// LAB-S02.Q06:R39-END

// LAB-S02.Q06:R40-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::leaf_reference(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] const ExecContext& context,
    [[maybe_unused]] const std::span<const types::Value> subplan_values
) const -> base::Result<std::reference_wrapper<const types::Value>> {
    return base::fail(
        expression_error(base::ErrorCode::kUnsupported, "S02.Q06:R40: load expression leaf", "lab")
    );
}

// LAB-S02.Q06:R40-END

// LAB-S02.Q06:R41-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::evaluate_leaf(
    [[maybe_unused]] const Instruction& instruction,
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] const ExecContext& context,
    [[maybe_unused]] const std::span<const types::Value> subplan_values
) const -> base::Result<types::Value> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R41: evaluate expression leaf",
        "lab"
    ));
}

// LAB-S02.Q06:R41-END

// LAB-S02.Q06:R42-BEGIN
[[maybe_unused]] auto
ExpressionProgram::Impl::bind_input_descriptor([[maybe_unused]] const Tuple& tuple) const
    -> base::Status {
    return base::fail(
        expression_error(base::ErrorCode::kUnsupported, "S02.Q06:R42: bind input descriptor", "lab")
    );
}

// LAB-S02.Q06:R42-END

// LAB-S02.Q06:R43-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::bind_parameter_descriptor(
    [[maybe_unused]] const ExecContext& context
) const -> base::Status {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R43: bind parameter descriptor",
        "lab"
    ));
}

// LAB-S02.Q06:R43-END

// LAB-S02.Q06:R44-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::evaluate(
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] const ExecContext& context,
    [[maybe_unused]] const std::span<const types::Value> subplan_values
) const -> base::Result<types::Value> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R44: evaluate expression program",
        "lab"
    ));
}

// LAB-S02.Q06:R44-END

// LAB-S02.Q06:R45-BEGIN
[[maybe_unused]] auto ExpressionProgram::Impl::evaluate_qualification(
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] const ExecContext& context,
    [[maybe_unused]] const std::span<const types::Value> subplan_values
) const -> base::Result<bool> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R45: evaluate expression qualification",
        "lab"
    ));
}

// LAB-S02.Q06:R45-END

ExpressionProgram::ExpressionProgram(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

ExpressionProgram::ExpressionProgram(ExpressionProgram&&) noexcept = default;
auto ExpressionProgram::operator=(ExpressionProgram&&) noexcept -> ExpressionProgram& = default;
ExpressionProgram::~ExpressionProgram() = default;

auto ExpressionProgram::valid() const noexcept -> bool {
    return implementation_ != nullptr;
}

// LAB-S02.Q06:R46-BEGIN
[[maybe_unused]] auto
ExpressionProgram::compile([[maybe_unused]] const planner::PlanExpression& expression)
    -> base::Result<ExpressionProgram> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R46: compile expression program",
        "lab"
    ));
}

// LAB-S02.Q06:R46-END

// LAB-S02.Q06:R47-BEGIN
[[maybe_unused]] auto ExpressionProgram::clone() const -> base::Result<ExpressionProgram> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R47: clone expression program",
        "lab"
    ));
}

// LAB-S02.Q06:R47-END

// LAB-S02.Q06:R48-BEGIN
[[maybe_unused]] auto ExpressionProgram::evaluate(
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] const ExecContext& context
) const -> base::Result<types::Value> {
    return base::fail(
        expression_error(base::ErrorCode::kUnsupported, "S02.Q06:R48: evaluate expression", "lab")
    );
}

// LAB-S02.Q06:R48-END

// LAB-S02.Q06:R49-BEGIN
[[maybe_unused]] auto ExpressionProgram::evaluate(
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] const ExecContext& context,
    [[maybe_unused]] const std::span<const types::Value> subplan_values
) const -> base::Result<types::Value> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R49: evaluate expression with subplans",
        "lab"
    ));
}

// LAB-S02.Q06:R49-END

// LAB-S02.Q06:R50-BEGIN
[[maybe_unused]] auto ExpressionProgram::evaluate_qualification(
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] const ExecContext& context
) const -> base::Result<bool> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R50: evaluate qualification",
        "lab"
    ));
}

// LAB-S02.Q06:R50-END

// LAB-S02.Q06:R51-BEGIN
[[maybe_unused]] auto ExpressionProgram::evaluate_qualification(
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] const ExecContext& context,
    [[maybe_unused]] const std::span<const types::Value> subplan_values
) const -> base::Result<bool> {
    return base::fail(expression_error(
        base::ErrorCode::kUnsupported,
        "S02.Q06:R51: evaluate qualification with subplans",
        "lab"
    ));
}

// LAB-S02.Q06:R51-END

}  // namespace db::execution
