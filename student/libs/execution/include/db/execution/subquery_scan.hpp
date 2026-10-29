#pragma once

#include <memory>
#include <optional>

#include "db/execution/expression.hpp"
#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// A derived-table boundary. The child is streamed without changing its
// descriptor, while the optional predicate keeps only SQL TRUE rows. The
// state owns the child and preserves its tuple RID when publishing output.
class SubqueryScanState final : public PlanState {
public:
    SubqueryScanState(const SubqueryScanState&) = delete;
    auto operator=(const SubqueryScanState&) -> SubqueryScanState& = delete;
    SubqueryScanState(SubqueryScanState&&) = delete;
    auto operator=(SubqueryScanState&&) -> SubqueryScanState& = delete;

    static auto create(const planner::SubqueryScanPlan& plan, std::unique_ptr<PlanState> input)
        -> base::Result<std::unique_ptr<SubqueryScanState>>;

    ~SubqueryScanState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    SubqueryScanState(
        TupleSlot output_slot,
        std::unique_ptr<PlanState> input,
        std::optional<ExpressionProgram> filter
    );

    std::unique_ptr<PlanState> input_;
    std::optional<ExpressionProgram> filter_;
};

}  // namespace db::execution
