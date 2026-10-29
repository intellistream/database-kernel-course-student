#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "db/execution/expression.hpp"
#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// Volcano nested-loop join. The outer tuple is taken from its child before the
// inner child is pulled, so a child output slot never outlives the call that
// produced it.
// Parameter bindings are written into the statement-local ExecContext before
// each inner open/rescan; IndexScanState can resolve PARAM_EXEC bounds without
// knowing anything about this join state.
class NestedLoopState final : public PlanState {
public:
    NestedLoopState(const NestedLoopState&) = delete;
    auto operator=(const NestedLoopState&) -> NestedLoopState& = delete;
    NestedLoopState(NestedLoopState&&) = delete;
    auto operator=(NestedLoopState&&) -> NestedLoopState& = delete;

    static auto create(
        const planner::NestedLoopPlan& plan,
        std::unique_ptr<PlanState> outer,
        std::unique_ptr<PlanState> inner
    ) -> base::Result<std::unique_ptr<NestedLoopState>>;

    ~NestedLoopState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    struct ParameterBinding final {
        planner::PlanParameterReference parameter;
        std::size_t outer_column{0U};
    };

    NestedLoopState(
        TupleSlot output_slot,
        std::unique_ptr<PlanState> outer,
        std::unique_ptr<PlanState> inner,
        std::optional<ExpressionProgram> join_filter,
        std::vector<ParameterBinding> parameters
    );

    auto
    bind_outer_parameters(std::span<const types::Value> outer_values, ExecContext& context) const
        -> base::Status;
    auto clear_parameters(ExecContext& context) const -> base::Status;
    auto prepare_inner(ExecContext& context) -> base::Status;
    auto advance_outer(ExecContext& context) -> base::Result<bool>;
    auto process_inner(ExecContext& context) -> base::Result<std::optional<bool>>;

    std::unique_ptr<PlanState> outer_;
    std::unique_ptr<PlanState> inner_;
    std::optional<ExpressionProgram> join_filter_;
    std::vector<ParameterBinding> parameters_;
    std::vector<types::Value> outer_values_;
    bool have_outer_{false};
    bool inner_opened_{false};
};

}  // namespace db::execution
