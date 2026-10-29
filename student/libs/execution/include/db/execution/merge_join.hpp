#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

#include "db/execution/expression.hpp"
#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// In-memory merge join over children that are already ordered by the planner's
// equality columns. Only the current inner duplicate group is retained, and
// it is replayed for every outer row with the same key. Ordering is a child
// contract; this state intentionally does not sort or materialize either full
// input.
class MergeJoinState final : public PlanState {
public:
    MergeJoinState(const MergeJoinState&) = delete;
    auto operator=(const MergeJoinState&) -> MergeJoinState& = delete;
    MergeJoinState(MergeJoinState&&) = delete;
    auto operator=(MergeJoinState&&) -> MergeJoinState& = delete;

    static auto create(
        const planner::MergeJoinPlan& plan,
        std::unique_ptr<PlanState> outer,
        std::unique_ptr<PlanState> inner
    ) -> base::Result<std::unique_ptr<MergeJoinState>>;

    ~MergeJoinState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    using Row = Tuple;

    enum class GroupStep : std::uint8_t {
        kEmitted,
        kSkipped,
    };

    MergeJoinState(
        TupleSlot output_slot,
        std::unique_ptr<PlanState> outer,
        std::unique_ptr<PlanState> inner,
        std::vector<std::size_t> outer_columns,
        std::vector<std::size_t> inner_columns,
        std::optional<ExpressionProgram> residual_filter
    );

    auto next_outer(ExecContext& context) -> base::Result<std::optional<Row>>;
    auto next_inner(ExecContext& context) -> base::Result<std::optional<Row>>;
    auto compare_keys(
        std::span<const types::Value> outer_values,
        std::span<const types::Value> inner_values
    ) const -> base::Result<std::strong_ordering>;
    auto compare_values(const types::Value& outer_value, const types::Value& inner_value) const
        -> base::Result<std::strong_ordering>;
    auto key_contains_null(
        std::span<const types::Value> values,
        std::span<const std::size_t> columns
    ) const noexcept -> bool;
    auto build_inner_group(ExecContext& context) -> base::Status;
    auto prepare_group(ExecContext& context) -> base::Status;
    auto process_group(ExecContext& context) -> base::Result<GroupStep>;
    auto emit_group_row(
        std::span<const types::Value> outer_values,
        std::span<const types::Value> inner_values,
        ExecContext& context
    ) -> base::Result<bool>;

    std::unique_ptr<PlanState> outer_;
    std::unique_ptr<PlanState> inner_;
    std::vector<std::size_t> outer_columns_;
    std::vector<std::size_t> inner_columns_;
    std::optional<ExpressionProgram> residual_filter_;
    std::optional<Row> outer_row_;
    std::optional<Row> inner_row_;
    std::vector<Row> inner_group_;
    std::size_t inner_group_pos_{0U};
    bool done_{false};
};

}  // namespace db::execution
