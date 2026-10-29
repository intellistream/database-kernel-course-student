#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "db/execution/expression.hpp"
#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"

namespace db::execution {

// In-memory inner hash join. The build side is fully materialized as owned
// values, while probe output remains in outer-major/inner-build order. The
// state deliberately exposes only the Volcano PlanState contract: planner
// expressions are compiled at construction and physical rows never escape a
// child call.
class HashJoinState final : public PlanState {
public:
    HashJoinState(const HashJoinState&) = delete;
    auto operator=(const HashJoinState&) -> HashJoinState& = delete;
    HashJoinState(HashJoinState&&) = delete;
    auto operator=(HashJoinState&&) -> HashJoinState& = delete;

    static auto create(
        const planner::HashJoinPlan& plan,
        std::unique_ptr<PlanState> outer,
        std::unique_ptr<PlanState> inner
    ) -> base::Result<std::unique_ptr<HashJoinState>>;

    ~HashJoinState() override = default;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    using JoinKey = std::vector<types::Value>;
    using RowValues = std::vector<types::Value>;

    struct JoinKeyHash final {
        auto operator()(const JoinKey& key) const noexcept -> std::size_t;
    };

    using HashTable = std::unordered_map<JoinKey, std::vector<Tuple>, JoinKeyHash>;

    HashJoinState(
        TupleSlot output_slot,
        std::unique_ptr<PlanState> outer,
        std::unique_ptr<PlanState> inner,
        std::vector<std::size_t> outer_columns,
        std::vector<std::size_t> inner_columns,
        std::optional<ExpressionProgram> residual_filter
    );

    auto build_inner(ExecContext& context) -> base::Status;
    auto fill_pending_from_outer(ExecContext& context) -> base::Result<bool>;
    auto next_outer_tuple(ExecContext& context) -> base::Result<std::optional<Tuple>>;
    auto append_match(
        std::span<const types::Value> outer_values,
        std::span<const types::Value> inner_values,
        ExecContext& context
    ) -> base::Result<bool>;
    auto append_matches(
        std::span<const types::Value> outer_values,
        const std::vector<Tuple>& matches,
        ExecContext& context
    ) -> base::Result<bool>;
    auto make_key(std::span<const types::Value> values, std::span<const std::size_t> columns) const
        -> std::optional<JoinKey>;

    std::unique_ptr<PlanState> outer_;
    std::unique_ptr<PlanState> inner_;
    std::vector<std::size_t> outer_columns_;
    std::vector<std::size_t> inner_columns_;
    std::optional<ExpressionProgram> residual_filter_;
    HashTable table_;
    std::vector<RowValues> pending_;
    std::size_t pending_pos_{0U};
};

}  // namespace db::execution
