#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

#include "db/execution/plan_state.hpp"
#include "db/execution/row_count.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// Streaming LIMIT/OFFSET state. It consumes only the rows needed for the
// requested window and preserves the first occurrence and RID of each row.
class LimitState final : public PlanState {
public:
    LimitState(const LimitState&) = delete;
    auto operator=(const LimitState&) -> LimitState& = delete;
    LimitState(LimitState&&) = delete;
    auto operator=(LimitState&&) -> LimitState& = delete;

    static auto create(const planner::LimitPlan& plan, std::unique_ptr<PlanState> input)
        -> base::Result<std::unique_ptr<LimitState>>;

    ~LimitState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    LimitState(TupleSlot output_slot, std::unique_ptr<PlanState> input, RowCountSupport row_count);

    std::unique_ptr<PlanState> input_;
    RowCountSupport row_count_;
    std::optional<std::size_t> limit_;
    std::size_t offset_{0U};
    std::size_t skipped_{0U};
    std::size_t emitted_{0U};
};

}  // namespace db::execution
