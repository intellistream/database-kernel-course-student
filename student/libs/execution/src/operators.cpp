#include "db/execution/operators.hpp"

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/execution/tuple.hpp"
#include "db/planner/plan.hpp"
#include "db/types/value.hpp"

#include "projection_program.hpp"

namespace db::execution {
namespace {

[[maybe_unused]] auto operator_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context("execution.operator", "construct");
}

[[maybe_unused]] auto descriptor_for_schema(const planner::PlanSchema& schema)
    -> base::Result<TupleDescriptorHandle> {
    return TupleDescriptor::from_plan_schema(schema);
}

[[maybe_unused]] auto empty_input_tuple() -> base::Result<Tuple> {
    auto descriptor = TupleDescriptor::create({});
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    return Tuple::create(std::move(descriptor).value(), {});
}

[[maybe_unused]] auto compile_expressions(
    const std::vector<planner::PlanExpressionPtr>& expressions,
    const std::string_view operation
) -> base::Result<std::vector<ExpressionProgram>> {
    std::vector<ExpressionProgram> compiled;
    compiled.reserve(expressions.size());
    for (const auto& expression : expressions) {
        if (expression == nullptr) {
            return base::fail(operator_error(
                base::ErrorCode::kPlan,
                std::string{operation} + " contains a null expression"
            ));
        }
        auto program = ExpressionProgram::compile(*expression);
        if (!program) {
            return base::fail(
                std::move(program).error().with_context(
                    "execution.operator",
                    std::string{operation}
                )
            );
        }
        compiled.push_back(std::move(program).value());
    }
    return compiled;
}

[[maybe_unused]] auto output_slot_for(const planner::PlanSchema& schema)
    -> base::Result<TupleSlot> {
    auto descriptor = descriptor_for_schema(schema);
    if (!descriptor) {
        return base::fail(std::move(descriptor).error());
    }
    return TupleSlot::create(std::move(descriptor).value());
}

}  // namespace

ValuesState::ValuesState(
    TupleSlot output_slot,
    Tuple input,
    std::vector<std::vector<ExpressionProgram>> rows
)
    : PlanState{std::move(output_slot)},
      input_{std::move(input)},
      rows_{std::move(rows)} {}

[[maybe_unused]] auto ValuesState::create([[maybe_unused]] const planner::ValuesPlan& plan)
    -> base::Result<std::unique_ptr<ValuesState>> {
    // LAB-S02.Q07:R01-BEGIN
    base::lab::missing_algorithm("S02.Q07:R01");
    // LAB-S02.Q07:R01-END
}

[[maybe_unused]] auto ValuesState::do_open([[maybe_unused]] ExecContext& context) -> base::Status {
    // LAB-S02.Q07:R02-BEGIN
    base::lab::missing_algorithm("S02.Q07:R02");
    // LAB-S02.Q07:R02-END
}

[[maybe_unused]] auto ValuesState::do_next([[maybe_unused]] ExecContext& context)
    -> base::Result<bool> {
    // LAB-S02.Q07:R03-BEGIN
    base::lab::missing_algorithm("S02.Q07:R03");
    // LAB-S02.Q07:R03-END
}

[[maybe_unused]] auto ValuesState::do_rescan([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S02.Q07:R04-BEGIN
    base::lab::missing_algorithm("S02.Q07:R04");
    // LAB-S02.Q07:R04-END
}

auto ValuesState::do_close(ExecContext& context) -> base::Status {
    static_cast<void>(context);
    return {};
}

ResultState::ResultState(
    TupleSlot output_slot,
    std::unique_ptr<PlanState> input,
    std::unique_ptr<ProjectionProgram> projection
)
    : PlanState{std::move(output_slot)},
      input_{std::move(input)},
      projection_{std::move(projection)} {}

ResultState::~ResultState() = default;

[[maybe_unused]] auto ResultState::create(
    [[maybe_unused]] const planner::ResultPlan& plan,
    [[maybe_unused]] std::unique_ptr<PlanState> input
) -> base::Result<std::unique_ptr<ResultState>> {
    // LAB-S02.Q07:R05-BEGIN
    base::lab::missing_algorithm("S02.Q07:R05");
    // LAB-S02.Q07:R05-END
}

[[maybe_unused]] auto ResultState::do_open([[maybe_unused]] ExecContext& context) -> base::Status {
    // LAB-S02.Q07:R06-BEGIN
    base::lab::missing_algorithm("S02.Q07:R06");
    // LAB-S02.Q07:R06-END
}

[[maybe_unused]] auto ResultState::do_next([[maybe_unused]] ExecContext& context)
    -> base::Result<bool> {
    // LAB-S02.Q07:R07-BEGIN
    base::lab::missing_algorithm("S02.Q07:R07");
    // LAB-S02.Q07:R07-END
}

[[maybe_unused]] auto ResultState::do_rescan([[maybe_unused]] ExecContext& context)
    -> base::Status {
    // LAB-S02.Q07:R08-BEGIN
    base::lab::missing_algorithm("S02.Q07:R08");
    // LAB-S02.Q07:R08-END
}

auto ResultState::do_close(ExecContext& context) -> base::Status {
    return input_->close(context);
}

}  // namespace db::execution
