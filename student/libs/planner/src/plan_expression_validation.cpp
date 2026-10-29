#include "plan_expression_validation.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/type_registry.hpp"

#include "plan_descriptor_validation.hpp"

namespace db::planner::detail {
namespace {

[[maybe_unused]] auto expression_error(std::string message) -> base::Error {
    return {base::ErrorCode::kPlan, std::move(message)};
}

[[maybe_unused]] auto is_boolean([[maybe_unused]] const types::DataType data_type) noexcept
    -> bool {
    // LAB-S04.Q08:R25-BEGIN
    base::lab::missing_algorithm("S04.Q08:R25");
    // LAB-S04.Q08:R25-END
}

[[maybe_unused]] auto is_numeric([[maybe_unused]] const types::DataType data_type) noexcept
    -> bool {
    // LAB-S04.Q08:R26-BEGIN
    base::lab::missing_algorithm("S04.Q08:R26");
    // LAB-S04.Q08:R26-END
}

template <typename... Visitors>
struct Overloaded final : Visitors... {
    using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

struct ExpressionValidationContext final {
    const PlanSchema& input_schema;
    const ParameterNamespace* parameters;
    SubplanReferenceValidation* subplan_references;
};

auto validate_expression_impl(
    const PlanExpression& expression,
    const ExpressionValidationContext& validation
) -> base::Status;
auto validate_expression_pointer_impl(
    const PlanExpressionPtr& expression,
    const ExpressionValidationContext& validation,
    std::string_view context
) -> base::Status;
auto validate_expression_list_impl(
    const std::vector<PlanExpressionPtr>& expressions,
    const ExpressionValidationContext& validation,
    std::string_view context,
    std::size_t minimum
) -> base::Status;

[[maybe_unused]] auto validate_literal(
    [[maybe_unused]] const PlanLiteralExpression& node,
    [[maybe_unused]] const PlanExpression& expression
) -> base::Status {
    // LAB-S04.Q08:R27-BEGIN
    base::lab::missing_algorithm("S04.Q08:R27");
    // LAB-S04.Q08:R27-END
}

[[maybe_unused]] auto validate_input_column(
    [[maybe_unused]] const PlanInputColumnReference& node,
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R28-BEGIN
    base::lab::missing_algorithm("S04.Q08:R28");
    // LAB-S04.Q08:R28-END
}

[[maybe_unused]] auto validate_parameter(
    [[maybe_unused]] const PlanParameterReference& node,
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R29-BEGIN
    base::lab::missing_algorithm("S04.Q08:R29");
    // LAB-S04.Q08:R29-END
}

[[maybe_unused]] auto validate_unary(
    [[maybe_unused]] const PlanUnaryExpression& node,
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R30-BEGIN
    base::lab::missing_algorithm("S04.Q08:R30");
    // LAB-S04.Q08:R30-END
}

[[maybe_unused]] auto validate_binary(
    [[maybe_unused]] const PlanBinaryExpression& node,
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R31-BEGIN
    base::lab::missing_algorithm("S04.Q08:R31");
    // LAB-S04.Q08:R31-END
}

[[maybe_unused]] auto validate_boolean(
    [[maybe_unused]] const PlanBooleanExpression& node,
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R32-BEGIN
    base::lab::missing_algorithm("S04.Q08:R32");
    // LAB-S04.Q08:R32-END
}

[[maybe_unused]] auto validate_is_null(
    [[maybe_unused]] const PlanIsNullExpression& node,
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R33-BEGIN
    base::lab::missing_algorithm("S04.Q08:R33");
    // LAB-S04.Q08:R33-END
}

[[maybe_unused]] auto validate_in_list(
    [[maybe_unused]] const PlanInListExpression& node,
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R34-BEGIN
    base::lab::missing_algorithm("S04.Q08:R34");
    // LAB-S04.Q08:R34-END
}

[[maybe_unused]] auto validate_between(
    [[maybe_unused]] const PlanBetweenExpression& node,
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R35-BEGIN
    base::lab::missing_algorithm("S04.Q08:R35");
    // LAB-S04.Q08:R35-END
}

[[maybe_unused]] auto validate_cast(
    [[maybe_unused]] const PlanCastExpression& node,
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R36-BEGIN
    base::lab::missing_algorithm("S04.Q08:R36");
    // LAB-S04.Q08:R36-END
}

[[maybe_unused]] auto validate_aggregate_expression(
    [[maybe_unused]] const PlanAggregateExpression& node,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R37-BEGIN
    base::lab::missing_algorithm("S04.Q08:R37");
    // LAB-S04.Q08:R37-END
}

[[maybe_unused]] auto validate_subplan_reference(
    [[maybe_unused]] const PlanSubPlanReference& node,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R38-BEGIN
    base::lab::missing_algorithm("S04.Q08:R38");
    // LAB-S04.Q08:R38-END
}

[[maybe_unused]] auto validate_expression_pointer_impl(
    [[maybe_unused]] const PlanExpressionPtr& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation,
    [[maybe_unused]] const std::string_view context
) -> base::Status {
    // LAB-S04.Q08:R39-BEGIN
    base::lab::missing_algorithm("S04.Q08:R39");
    // LAB-S04.Q08:R39-END
}

[[maybe_unused]] auto validate_expression_list_impl(
    [[maybe_unused]] const std::vector<PlanExpressionPtr>& expressions,
    [[maybe_unused]] const ExpressionValidationContext& validation,
    [[maybe_unused]] const std::string_view context,
    [[maybe_unused]] const std::size_t minimum
) -> base::Status {
    // LAB-S04.Q08:R40-BEGIN
    base::lab::missing_algorithm("S04.Q08:R40");
    // LAB-S04.Q08:R40-END
}

[[maybe_unused]] auto validate_expression_impl(
    [[maybe_unused]] const PlanExpression& expression,
    [[maybe_unused]] const ExpressionValidationContext& validation
) -> base::Status {
    // LAB-S04.Q08:R41-BEGIN
    base::lab::missing_algorithm("S04.Q08:R41");
    // LAB-S04.Q08:R41-END
}
}  // namespace

[[maybe_unused]] auto validate_expression_pointer(
    [[maybe_unused]] const PlanExpressionPtr& expression,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] const std::string_view context,
    [[maybe_unused]] SubplanReferenceValidation* const subplan_references
) -> base::Status {
    // LAB-S04.Q08:R42-BEGIN
    base::lab::missing_algorithm("S04.Q08:R42");
    // LAB-S04.Q08:R42-END
}

[[maybe_unused]] auto validate_optional_expression(
    [[maybe_unused]] const PlanExpressionPtr& expression,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] SubplanReferenceValidation* const subplan_references
) -> base::Status {
    // LAB-S04.Q08:R43-BEGIN
    base::lab::missing_algorithm("S04.Q08:R43");
    // LAB-S04.Q08:R43-END
}

[[maybe_unused]] auto validate_expression_list(
    [[maybe_unused]] const std::vector<PlanExpressionPtr>& expressions,
    [[maybe_unused]] const PlanSchema& input_schema,
    [[maybe_unused]] const ParameterNamespace* const parameters,
    [[maybe_unused]] const std::string_view context,
    [[maybe_unused]] const std::size_t minimum,
    [[maybe_unused]] SubplanReferenceValidation* const subplan_references
) -> base::Status {
    // LAB-S04.Q08:R44-BEGIN
    base::lab::missing_algorithm("S04.Q08:R44");
    // LAB-S04.Q08:R44-END
}

[[maybe_unused]] auto
finish_subplan_reference_validation([[maybe_unused]] const SubplanReferenceValidation& validation)
    -> base::Status {
    // LAB-S04.Q08:R45-BEGIN
    base::lab::missing_algorithm("S04.Q08:R45");
    // LAB-S04.Q08:R45-END
}
}  // namespace db::planner::detail
