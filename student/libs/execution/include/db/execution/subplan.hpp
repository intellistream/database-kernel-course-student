#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

#include "db/execution/expression.hpp"
#include "db/execution/plan_state.hpp"
#include "db/execution/plan_state_factory.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// Evaluates planner-owned SubPlan values for each row of a passthrough input.
// Subplans run in declaration order so later argument expressions can consume
// earlier results. Executor parameters are bound only for the current child
// evaluation and are cleared before the state publishes its next row.
class SubPlanEvalState final : public PlanState {
public:
    SubPlanEvalState(const SubPlanEvalState&) = delete;
    auto operator=(const SubPlanEvalState&) -> SubPlanEvalState& = delete;
    SubPlanEvalState(SubPlanEvalState&&) = delete;
    auto operator=(SubPlanEvalState&&) -> SubPlanEvalState& = delete;

    static auto create(
        const planner::SubPlanEvalPlan& plan,
        std::unique_ptr<PlanState> input,
        PlanStateFactory& factory
    ) -> base::Result<std::unique_ptr<SubPlanEvalState>>;

    ~SubPlanEvalState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    struct SubPlanState final {
        std::unique_ptr<PlanState> plan;
        planner::PlanSubPlanStrategy strategy{planner::PlanSubPlanStrategy::kScalar};
        std::optional<ExpressionProgram> input_expression;
        std::vector<ExpressionProgram> arguments;
        std::vector<planner::PlanParameterReference> parameters;
        bool uses_inherited_parameters{false};
        std::optional<types::Value> cached;
        std::unordered_set<types::Value> membership_values;
        bool membership_has_rows{false};
        bool membership_has_null{false};
        bool membership_materialized{false};
    };

    SubPlanEvalState(
        TupleSlot output_slot,
        std::unique_ptr<PlanState> input,
        std::vector<SubPlanState> subplans,
        std::optional<ExpressionProgram> filter
    );

    static auto
    create_subplan_state(const planner::PlanSubPlan& specification, PlanStateFactory& factory)
        -> base::Result<SubPlanState>;
    auto next_input(ExecContext& context) -> base::Result<std::optional<const Tuple*>>;
    auto process_input_tuple(const Tuple& tuple, ExecContext& context) -> base::Result<bool>;

    auto evaluate_subplan(
        SubPlanState& subplan,
        const Tuple& outer,
        ExecContext& context,
        std::span<const types::Value> preceding_values
    ) -> base::Result<types::Value>;
    auto evaluate_exists(SubPlanState& subplan, ExecContext& context) -> base::Result<types::Value>;
    auto evaluate_scalar(SubPlanState& subplan, ExecContext& context) -> base::Result<types::Value>;
    auto evaluate_membership(
        SubPlanState& subplan,
        const Tuple& outer,
        ExecContext& context,
        std::span<const types::Value> preceding_values,
        bool materialize
    ) -> base::Result<types::Value>;
    static auto membership_result(planner::PlanSubPlanStrategy strategy, bool found, bool unknown)
        -> types::Value;
    static void record_membership_candidate(SubPlanState& subplan, const types::Value& candidate);
    auto scan_membership(
        SubPlanState& subplan,
        const types::Value& test,
        ExecContext& context,
        bool materialize
    ) -> base::Result<types::Value>;
    auto bind_arguments(
        SubPlanState& subplan,
        const Tuple& outer,
        ExecContext& context,
        std::span<const types::Value> preceding_values
    ) const -> base::Status;
    auto clear_parameters(const SubPlanState& subplan, ExecContext& context) const -> base::Status;
    auto open_children(ExecContext& context) -> base::Status;
    auto close_children(ExecContext& context) -> base::Status;
    void clear_cached_results() noexcept;

    std::unique_ptr<PlanState> input_;
    std::vector<SubPlanState> subplans_;
    std::optional<ExpressionProgram> filter_;
    std::vector<types::Value> subplan_values_;
};

}  // namespace db::execution
