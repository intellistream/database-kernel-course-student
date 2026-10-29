#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "db/execution/expression.hpp"
#include "db/execution/plan_state.hpp"
#include "db/planner/plan.hpp"
#include "db/types/value.hpp"

namespace db::execution {

// Hash/group aggregate executor. The child is consumed once per open and the
// resulting groups are published in first-seen order, so hash-table layout
// never changes observable SQL output order. Aggregate expressions are
// compiled once and own no binder or planner state after construction.
class AggregateState final : public PlanState {
public:
    AggregateState(const AggregateState&) = delete;
    auto operator=(const AggregateState&) -> AggregateState& = delete;
    AggregateState(AggregateState&&) = delete;
    auto operator=(AggregateState&&) -> AggregateState& = delete;

    static auto create(const planner::AggregatePlan& plan, std::unique_ptr<PlanState> input)
        -> base::Result<std::unique_ptr<AggregateState>>;

    ~AggregateState() override;

protected:
    auto do_open(ExecContext& context) -> base::Status override;
    auto do_next(ExecContext& context) -> base::Result<bool> override;
    auto do_rescan(ExecContext& context) -> base::Status override;
    auto do_close(ExecContext& context) -> base::Status override;

private:
    struct ValueHash final {
        auto operator()(const types::Value& value) const noexcept -> std::size_t;
    };

    struct GroupKeyHash final {
        auto operator()(const std::vector<types::Value>& values) const noexcept -> std::size_t;
    };

    struct AggregateSpec final {
        planner::PlanAggregateKind kind{planner::PlanAggregateKind::kCount};
        bool distinct{false};
        bool star{false};
        types::DataType input_type{types::kUnknownType};
        types::DataType result_type{types::kUnknownType};
        std::optional<ExpressionProgram> argument;
    };

    struct Accumulator;

    struct Group final {
        std::vector<types::Value> key;
        std::vector<Accumulator> accumulators;
    };

    using GroupIndex = std::unordered_map<std::vector<types::Value>, std::size_t, GroupKeyHash>;

    AggregateState(
        TupleSlot output_slot,
        std::unique_ptr<PlanState> input,
        std::vector<ExpressionProgram> group_by,
        std::vector<AggregateSpec> aggregates,
        std::optional<ExpressionProgram> having,
        bool ordered_min
    );

    auto compute(ExecContext& context) -> base::Status;
    static auto compile_group_by(const planner::AggregatePlan& plan)
        -> base::Result<std::vector<ExpressionProgram>>;
    static auto compile_aggregate(const planner::PlanAggregate& aggregate)
        -> base::Result<AggregateSpec>;
    static auto compile_having(const planner::AggregatePlan& plan)
        -> base::Result<std::optional<ExpressionProgram>>;
    auto make_group(const std::vector<types::Value>& key) const -> base::Result<Group>;
    auto evaluate_group_key(
        const Tuple& tuple,
        const ExecContext& context,
        std::vector<types::Value>& key
    ) const -> base::Status;
    auto advance_group(Group& group, const Tuple& tuple, ExecContext& context) -> base::Status;
    auto finish_groups(const std::vector<Group>& groups, ExecContext& context) -> base::Status;
    auto ordered_min_complete(const Group& group) const noexcept -> bool;
    auto find_or_create_group(
        std::vector<Group>& groups,
        GroupIndex& group_index,
        const std::vector<types::Value>& key
    ) const -> base::Result<Group*>;
    auto passes_having(const std::vector<types::Value>& values, const ExecContext& context) const
        -> base::Result<bool>;

    std::unique_ptr<PlanState> input_;
    std::vector<ExpressionProgram> group_by_;
    std::vector<AggregateSpec> aggregates_;
    std::optional<ExpressionProgram> having_;
    bool ordered_min_{false};
    std::vector<std::vector<types::Value>> output_rows_;
    std::size_t next_output_{0U};
    bool computed_{false};
};

}  // namespace db::execution
