#pragma once

#include <cstddef>
#include <memory>

#include "db/execution/modify_table.hpp"
#include "db/execution/plan_state_factory.hpp"
#include "db/planner/plan.hpp"

namespace db::engine::detail {

struct QueryExecutionContext;

// The caller must destroy the returned tree before this factory. The factory owns
// the modification port, and query context/services outlive both owners.
class StoragePlanFactory final : public execution::PlanStateFactory {
public:
    explicit StoragePlanFactory(QueryExecutionContext& context);

    auto create(const planner::Plan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>> override;

private:
    auto create_node(const planner::ValuesPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::SeqScanPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_seq_scan(const planner::SeqScanPlan& plan, const planner::ResultPlan* result)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::IndexScanPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_index_scan(const planner::IndexScanPlan& plan, const planner::ResultPlan* result)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::ResultPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::ModifyTablePlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    template <typename PlanType, typename StateType>
    auto create_unary(
        const PlanType& plan,
        base::Result<std::unique_ptr<StateType>> (*creator)(
            const PlanType&,
            std::unique_ptr<execution::PlanState>
        )
    ) -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::SortPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::LimitPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::TopNPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::AppendPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::UniquePlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::AggregatePlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::SubqueryScanPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::NestedLoopPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::MergeJoinPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::HashJoinPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    template <typename PlanType, typename StateType>
    auto create_join(
        const PlanType& plan,
        base::Result<std::unique_ptr<StateType>> (*creator)(
            const PlanType&,
            std::unique_ptr<execution::PlanState>,
            std::unique_ptr<execution::PlanState>
        )
    ) -> base::Result<std::unique_ptr<execution::PlanState>>;

    auto create_node(const planner::SubPlanEvalPlan& plan)
        -> base::Result<std::unique_ptr<execution::PlanState>>;

    QueryExecutionContext& context_;
    std::unique_ptr<execution::ModifyTablePort> modifier_;
    // Borrowed from the returned execution tree; read only while that tree is alive.
    execution::ModifyTableState* modify_state_{nullptr};

public:
    auto affected_rows() const noexcept -> std::size_t;
};

}  // namespace db::engine::detail
