#include "db/execution/subplan.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "state_support.hpp"

namespace db::execution {

namespace {

[[maybe_unused]] auto subplan_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.subplan", "construct");
}

[[maybe_unused]] auto subplan_runtime_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kExecution, std::move(message)}.with_context(
        "execution.subplan",
        "run"
    );
}

[[maybe_unused]] auto is_membership(const planner::PlanSubPlanStrategy strategy) noexcept -> bool {
    return strategy == planner::PlanSubPlanStrategy::kIn
           || strategy == planner::PlanSubPlanStrategy::kNotIn;
}

[[maybe_unused]] auto is_scalar(const planner::PlanSubPlanStrategy strategy) noexcept -> bool {
    return strategy == planner::PlanSubPlanStrategy::kScalar;
}

[[maybe_unused]] auto is_known_strategy(const planner::PlanSubPlanStrategy strategy) noexcept
    -> bool {
    switch (strategy) {
        case planner::PlanSubPlanStrategy::kIn:
        case planner::PlanSubPlanStrategy::kNotIn:
        case planner::PlanSubPlanStrategy::kExists:
        case planner::PlanSubPlanStrategy::kNotExists:
        case planner::PlanSubPlanStrategy::kScalar:
            return true;
    }
    return false;
}

[[maybe_unused]] auto validate_subplan_specification(
    const planner::PlanSubPlan& specification,
    const TupleDescriptorHandle& child_descriptor
) -> base::Status {
    if (!is_known_strategy(specification.strategy)) {
        return base::fail(subplan_error(base::ErrorCode::kPlan, "subplan has an unknown strategy"));
    }
    if (specification.arguments.size() != specification.parameters.size()) {
        return base::fail(subplan_error(
            base::ErrorCode::kPlan,
            "subplan arguments and parameter mappings differ in length"
        ));
    }
    const bool membership = is_membership(specification.strategy);
    if ((membership || is_scalar(specification.strategy))
        && (child_descriptor == nullptr || child_descriptor->size() != 1U)) {
        return base::fail(subplan_error(
            base::ErrorCode::kPlan,
            "IN and scalar subplans require one child output column"
        ));
    }
    if (membership && specification.input_expression == nullptr) {
        return base::fail(
            subplan_error(base::ErrorCode::kPlan, "IN subplan is missing its input expression")
        );
    }
    if (!membership && specification.input_expression != nullptr) {
        return base::fail(
            subplan_error(base::ErrorCode::kPlan, "non-IN subplan has an input expression")
        );
    }
    if (membership
        && specification.input_expression->data_type
               != child_descriptor->columns().front().data_type) {
        return base::fail(
            subplan_error(base::ErrorCode::kPlan, "IN subplan input type differs from child output")
        );
    }
    return {};
}

[[maybe_unused]] auto compile_subplan_input(const planner::PlanSubPlan& specification)
    -> base::Result<std::optional<ExpressionProgram>> {
    if (specification.input_expression == nullptr) {
        return std::optional<ExpressionProgram>{};
    }
    auto compiled = ExpressionProgram::compile(*specification.input_expression);
    if (!compiled) {
        return base::fail(std::move(compiled).error());
    }
    return std::optional<ExpressionProgram>{std::move(compiled).value()};
}

[[maybe_unused]] auto repeats_parameter(
    const std::vector<planner::PlanParameterReference>& parameters,
    const std::size_t index
) -> bool {
    for (std::size_t prior = 0U; prior < index; ++prior) {
        if (parameters[prior].id == parameters[index].id) {
            return true;
        }
    }
    return false;
}

[[maybe_unused]] auto
validate_argument_pair(const planner::PlanSubPlan& specification, const std::size_t index)
    -> base::Status {
    const planner::PlanExpressionPtr& argument = specification.arguments[index];
    if (argument == nullptr) {
        return base::fail(subplan_error(base::ErrorCode::kPlan, "subplan argument is absent"));
    }
    const planner::PlanParameterReference& parameter = specification.parameters[index];
    if (parameter.kind != planner::PlanParameterKind::kExecutor
        || argument->data_type != parameter.data_type) {
        return base::fail(subplan_error(
            base::ErrorCode::kPlan,
            "subplan argument does not match its executor parameter"
        ));
    }
    if (repeats_parameter(specification.parameters, index)) {
        return base::fail(
            subplan_error(base::ErrorCode::kPlan, "subplan executor parameter repeats")
        );
    }
    return {};
}

[[maybe_unused]] auto compile_subplan_arguments(const planner::PlanSubPlan& specification)
    -> base::Result<std::vector<ExpressionProgram>> {
    std::vector<ExpressionProgram> arguments;
    arguments.reserve(specification.arguments.size());
    for (std::size_t index = 0U; index < specification.arguments.size(); ++index) {
        auto valid = validate_argument_pair(specification, index);
        if (!valid) {
            return base::fail(std::move(valid).error());
        }
        auto compiled = ExpressionProgram::compile(*specification.arguments[index]);
        if (!compiled) {
            return base::fail(std::move(compiled).error());
        }
        arguments.push_back(std::move(compiled).value());
    }
    return arguments;
}

[[maybe_unused]] auto compile_filter(const planner::PlanExpressionPtr& expression)
    -> base::Result<std::optional<ExpressionProgram>> {
    if (expression == nullptr) {
        return std::optional<ExpressionProgram>{};
    }
    if (expression->data_type != types::kBoolType) {
        return base::fail(subplan_error(base::ErrorCode::kPlan, "subplan-eval filter is not BOOL"));
    }
    auto compiled = ExpressionProgram::compile(*expression);
    if (!compiled) {
        return base::fail(std::move(compiled).error());
    }
    return std::optional<ExpressionProgram>{std::move(compiled).value()};
}

}  // namespace

SubPlanEvalState::SubPlanEvalState(
    TupleSlot output_slot,
    std::unique_ptr<PlanState> input,
    std::vector<SubPlanState> subplans,
    std::optional<ExpressionProgram> filter
)
    : PlanState{std::move(output_slot)},
      input_{std::move(input)},
      subplans_{std::move(subplans)},
      filter_{std::move(filter)} {}

auto SubPlanEvalState::create(
    const planner::SubPlanEvalPlan& plan,
    std::unique_ptr<PlanState> input,
    PlanStateFactory& factory
) -> base::Result<std::unique_ptr<SubPlanEvalState>> {
    if (input == nullptr) {
        return base::fail(subplan_error(base::ErrorCode::kPlan, "subplan-eval input is absent"));
    }
    auto slot = detail::output_slot_for_schema(plan.metadata.output);
    if (!slot) {
        return base::fail(std::move(slot).error());
    }
    const TupleDescriptorHandle input_descriptor = input->output_slot().descriptor();
    auto shape =
        detail::validate_passthrough_shape(input_descriptor, slot->descriptor(), "subplan-eval");
    if (!shape) {
        return base::fail(std::move(shape).error());
    }

    auto filter = compile_filter(plan.filter);
    if (!filter) {
        return base::fail(std::move(filter).error());
    }

    std::vector<SubPlanState> subplans;
    subplans.reserve(plan.subplans.size());
    for (const planner::PlanSubPlan& specification : plan.subplans) {
        auto subplan = create_subplan_state(specification, factory);
        if (!subplan) {
            return base::fail(std::move(subplan).error());
        }
        subplans.push_back(std::move(subplan).value());
    }

    return std::unique_ptr<SubPlanEvalState>{new SubPlanEvalState{
        std::move(slot).value(),
        std::move(input),
        std::move(subplans),
        std::move(filter).value(),
    }};
}

auto SubPlanEvalState::create_subplan_state(
    const planner::PlanSubPlan& specification,
    PlanStateFactory& factory
) -> base::Result<SubPlanEvalState::SubPlanState> {
    if (specification.plan == nullptr) {
        return base::fail(subplan_error(base::ErrorCode::kPlan, "subplan child is absent"));
    }
    auto child = factory.create(*specification.plan);
    if (!child) {
        return base::fail(
            std::move(child).error().with_context("execution.subplan", "create child")
        );
    }
    if (child.value() == nullptr) {
        return base::fail(
            subplan_error(base::ErrorCode::kPlan, "subplan factory returned a null child")
        );
    }
    auto valid =
        validate_subplan_specification(specification, child.value()->output_slot().descriptor());
    if (!valid) {
        return base::fail(std::move(valid).error());
    }
    auto input_expression = compile_subplan_input(specification);
    if (!input_expression) {
        return base::fail(std::move(input_expression).error());
    }
    auto arguments = compile_subplan_arguments(specification);
    if (!arguments) {
        return base::fail(std::move(arguments).error());
    }
    return SubPlanState{
        .plan = std::move(child).value(),
        .strategy = specification.strategy,
        .input_expression = std::move(input_expression).value(),
        .arguments = std::move(arguments).value(),
        .parameters = specification.parameters,
        .uses_inherited_parameters = specification.uses_inherited_parameters,
        .cached = std::nullopt,
        .membership_values = {},
        .membership_has_rows = false,
        .membership_has_null = false,
        .membership_materialized = false,
    };
}

// LAB-S07.Q07:R01-BEGIN
[[maybe_unused]] auto SubPlanEvalState::open_children([[maybe_unused]] ExecContext& context)
    -> base::Status {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R01: open subplan children"}
    );
}

// LAB-S07.Q07:R01-END

// LAB-S07.Q07:R02-BEGIN
[[maybe_unused]] void SubPlanEvalState::clear_cached_results() noexcept {
    base::lab::missing_algorithm("S07.Q07:R02");
}

// LAB-S07.Q07:R02-END

// LAB-S07.Q07:R03-BEGIN
[[maybe_unused]] auto SubPlanEvalState::do_open([[maybe_unused]] ExecContext& context)
    -> base::Status {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R03: open subplan evaluation"}
    );
}

// LAB-S07.Q07:R03-END

// LAB-S07.Q07:R04-BEGIN
[[maybe_unused]] auto SubPlanEvalState::evaluate_exists(
    [[maybe_unused]] SubPlanState& subplan,
    [[maybe_unused]] ExecContext& context
) -> base::Result<types::Value> {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R04: evaluate EXISTS subplan"}
    );
}

// LAB-S07.Q07:R04-END

// LAB-S07.Q07:R05-BEGIN
[[maybe_unused]] auto SubPlanEvalState::evaluate_scalar(
    [[maybe_unused]] SubPlanState& subplan,
    [[maybe_unused]] ExecContext& context
) -> base::Result<types::Value> {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R05: evaluate scalar subplan"}
    );
}

// LAB-S07.Q07:R05-END

// LAB-S07.Q07:R06-BEGIN
[[maybe_unused]] auto SubPlanEvalState::membership_result(
    [[maybe_unused]] const planner::PlanSubPlanStrategy strategy,
    [[maybe_unused]] const bool found,
    [[maybe_unused]] const bool unknown
) -> types::Value {
    base::lab::missing_algorithm("S07.Q07:R06");
}

// LAB-S07.Q07:R06-END

// LAB-S07.Q07:R07-BEGIN
[[maybe_unused]] void SubPlanEvalState::record_membership_candidate(
    [[maybe_unused]] SubPlanState& subplan,
    [[maybe_unused]] const types::Value& candidate
) {
    base::lab::missing_algorithm("S07.Q07:R07");
}

// LAB-S07.Q07:R07-END

// LAB-S07.Q07:R08-BEGIN
[[maybe_unused]] auto SubPlanEvalState::scan_membership(
    [[maybe_unused]] SubPlanState& subplan,
    [[maybe_unused]] const types::Value& test,
    [[maybe_unused]] ExecContext& context,
    [[maybe_unused]] const bool materialize
) -> base::Result<types::Value> {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R08: scan membership subplan"}
    );
}

// LAB-S07.Q07:R08-END

// LAB-S07.Q07:R09-BEGIN
[[maybe_unused]] auto SubPlanEvalState::evaluate_membership(
    [[maybe_unused]] SubPlanState& subplan,
    [[maybe_unused]] const Tuple& outer,
    [[maybe_unused]] ExecContext& context,
    [[maybe_unused]] const std::span<const types::Value> preceding_values,
    [[maybe_unused]] const bool materialize
) -> base::Result<types::Value> {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R09: evaluate membership subplan"}
    );
}

// LAB-S07.Q07:R09-END

// LAB-S07.Q07:R10-BEGIN
[[maybe_unused]] auto SubPlanEvalState::bind_arguments(
    [[maybe_unused]] SubPlanState& subplan,
    [[maybe_unused]] const Tuple& outer,
    [[maybe_unused]] ExecContext& context,
    [[maybe_unused]] const std::span<const types::Value> preceding_values
) const -> base::Status {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R10: bind correlated subplan arguments"}
    );
}

// LAB-S07.Q07:R10-END

// LAB-S07.Q07:R11-BEGIN
[[maybe_unused]] auto SubPlanEvalState::clear_parameters(
    [[maybe_unused]] const SubPlanState& subplan,
    [[maybe_unused]] ExecContext& context
) const -> base::Status {
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "S07.Q07:R11: clear correlated subplan parameters"
        }
    );
}

// LAB-S07.Q07:R11-END

// LAB-S07.Q07:R12-BEGIN
[[maybe_unused]] auto SubPlanEvalState::evaluate_subplan(
    [[maybe_unused]] SubPlanState& subplan,
    [[maybe_unused]] const Tuple& outer,
    [[maybe_unused]] ExecContext& context,
    [[maybe_unused]] const std::span<const types::Value> preceding_values
) -> base::Result<types::Value> {
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R12: evaluate subplan"});
}

// LAB-S07.Q07:R12-END

// LAB-S07.Q07:R13-BEGIN
[[maybe_unused]] auto SubPlanEvalState::next_input([[maybe_unused]] ExecContext& context)
    -> base::Result<std::optional<const Tuple*>> {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R13: advance subplan input"}
    );
}

// LAB-S07.Q07:R13-END

// LAB-S07.Q07:R14-BEGIN
[[maybe_unused]] auto SubPlanEvalState::process_input_tuple(
    [[maybe_unused]] const Tuple& tuple,
    [[maybe_unused]] ExecContext& context
) -> base::Result<bool> {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R14: process subplan input tuple"}
    );
}

// LAB-S07.Q07:R14-END

// LAB-S07.Q07:R15-BEGIN
[[maybe_unused]] auto SubPlanEvalState::do_next([[maybe_unused]] ExecContext& context)
    -> base::Result<bool> {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R15: advance subplan evaluation"}
    );
}

// LAB-S07.Q07:R15-END

// LAB-S07.Q07:R16-BEGIN
[[maybe_unused]] auto SubPlanEvalState::do_rescan([[maybe_unused]] ExecContext& context)
    -> base::Status {
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S07.Q07:R16: rescan subplan evaluation"}
    );
}

// LAB-S07.Q07:R16-END

auto SubPlanEvalState::close_children(ExecContext& context) -> base::Status {
    std::optional<base::Error> first_error;
    for (std::size_t index = subplans_.size(); index > 0U; --index) {
        auto closed = subplans_[index - 1U].plan->close(context);
        if (!closed && !first_error.has_value()) {
            first_error = std::move(closed).error();
        }
    }
    auto input_closed = input_->close(context);
    if (!input_closed && !first_error.has_value()) {
        first_error = std::move(input_closed).error();
    }
    if (first_error.has_value()) {
        return base::fail(std::move(first_error).value());
    }
    return {};
}

auto SubPlanEvalState::do_close(ExecContext& context) -> base::Status {
    subplan_values_.clear();
    clear_cached_results();
    return close_children(context);
}

}  // namespace db::execution
