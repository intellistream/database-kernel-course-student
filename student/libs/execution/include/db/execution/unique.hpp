#pragma once

#include <cstddef>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// Streaming SELECT DISTINCT state. It emits the first row for each complete
// value vector and retains no planner, catalog, or physical access state.
class UniqueState final : public PlanState {
public:
    UniqueState(const UniqueState&) = delete;
    auto operator=(const UniqueState&) -> UniqueState& = delete;
    UniqueState(UniqueState&&) = delete;
    auto operator=(UniqueState&&) -> UniqueState& = delete;

    static auto create(const planner::UniquePlan& plan, std::unique_ptr<PlanState> input)
        -> base::Result<std::unique_ptr<UniqueState>>;

    ~UniqueState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    struct ValuesHash final {
        auto operator()(const std::vector<types::Value>& values) const noexcept -> std::size_t;
    };

    using SeenValues = std::unordered_set<std::vector<types::Value>, ValuesHash>;

    UniqueState(TupleSlot output_slot, std::unique_ptr<PlanState> input)
        : PlanState{std::move(output_slot)},
          input_{std::move(input)} {}

    std::unique_ptr<PlanState> input_;
    SeenValues seen_;
};

}  // namespace db::execution
