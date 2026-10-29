#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {
// Streams one child at a time, retaining no rows. Unvisited children are opened
// lazily; rescan rewinds opened children and leaves unvisited children untouched.
class AppendState final : public PlanState {
public:
    static auto
    create(const planner::AppendPlan& plan, std::vector<std::unique_ptr<PlanState>> inputs)
        -> base::Result<std::unique_ptr<AppendState>>;
    ~AppendState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    AppendState(TupleSlot slot, std::vector<std::unique_ptr<PlanState>> inputs)
        : PlanState{std::move(slot)},
          inputs_{std::move(inputs)} {}

    std::vector<std::unique_ptr<PlanState>> inputs_;
    std::size_t current_{0U};
};
}  // namespace db::execution
