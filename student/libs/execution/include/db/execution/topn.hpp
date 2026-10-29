#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "db/execution/plan_state.hpp"
#include "db/execution/row_count.hpp"
#include "db/execution/sort_support.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// Bounded ORDER BY + LIMIT/OFFSET state. At most limit+offset rows and their
// evaluated keys are retained while the child is drained.
class TopNState final : public PlanState {
public:
    TopNState(const TopNState&) = delete;
    auto operator=(const TopNState&) -> TopNState& = delete;
    TopNState(TopNState&&) = delete;
    auto operator=(TopNState&&) -> TopNState& = delete;

    static auto create(const planner::TopNPlan& plan, std::unique_ptr<PlanState> input)
        -> base::Result<std::unique_ptr<TopNState>>;

    ~TopNState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    struct Entry final {
        Tuple tuple;
        SortKeyValues keys;
        std::size_t sequence{0U};
    };

    TopNState(
        TupleSlot output_slot,
        std::unique_ptr<PlanState> input,
        SortSupport support,
        RowCountSupport row_count
    );

    auto entry_precedes(const Entry& left, const Entry& right) const noexcept -> bool;
    auto entry_precedes(
        std::span<const types::Value> keys,
        std::size_t sequence,
        const Entry& right
    ) const noexcept -> bool;
    auto fill(ExecContext& context) -> base::Status;
    void replace_worst(Entry replacement);
    auto resolve_window(const ExecContext& context) -> base::Status;

    std::unique_ptr<PlanState> input_;
    SortSupport support_;
    RowCountSupport row_count_;
    std::vector<Entry> heap_;
    std::size_t limit_{0U};
    std::size_t offset_{0U};
    std::size_t next_entry_{0U};
    std::size_t capacity_{0U};
    bool filled_{false};
};

}  // namespace db::execution
