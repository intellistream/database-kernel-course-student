#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "db/execution/plan_state.hpp"
#include "db/execution/sort_support.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// Full materializing sort. Child rows and evaluated keys are owned until the
// next rescan or close; std::stable_sort preserves child order for equal keys.
class SortState final : public PlanState {
public:
    SortState(const SortState&) = delete;
    auto operator=(const SortState&) -> SortState& = delete;
    SortState(SortState&&) = delete;
    auto operator=(SortState&&) -> SortState& = delete;

    static auto create(const planner::SortPlan& plan, std::unique_ptr<PlanState> input)
        -> base::Result<std::unique_ptr<SortState>>;

    ~SortState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    struct Entry final {
        Tuple tuple;
        SortKeyValues keys;
    };

    SortState(TupleSlot output_slot, std::unique_ptr<PlanState> input, SortSupport support);

    auto materialize_child(ExecContext& context) -> base::Status;

    std::unique_ptr<PlanState> input_;
    SortSupport support_;
    std::vector<Entry> entries_;
    std::size_t next_entry_{0U};
    bool sorted_{false};
};

}  // namespace db::execution
