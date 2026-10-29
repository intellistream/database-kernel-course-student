#include "storage_plan_factory.hpp"

#include <algorithm>
#include <utility>
#include <variant>

#include "db/base/lab_missing.hpp"
#include "db/execution/aggregate.hpp"
#include "db/execution/append.hpp"
#include "db/execution/hash_join.hpp"
#include "db/execution/limit.hpp"
#include "db/execution/merge_join.hpp"
#include "db/execution/nested_loop.hpp"
#include "db/execution/operators.hpp"
#include "db/execution/sort.hpp"
#include "db/execution/subplan.hpp"
#include "db/execution/subquery_scan.hpp"
#include "db/execution/topn.hpp"
#include "db/execution/unique.hpp"

#include "logical_scan_ssi.hpp"
#include "physical_modify_port.hpp"
#include "query_error.hpp"
#include "storage_scan_ports.hpp"

namespace db::engine::detail {

namespace {

template <typename State>
[[maybe_unused]] auto as_plan_state([[maybe_unused]] base::Result<std::unique_ptr<State>> state)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R46-BEGIN
    base::lab::missing_algorithm("S02.Q11:R46");
    // LAB-S02.Q11:R46-END
}

[[maybe_unused]] auto
index_scan_projects_columns([[maybe_unused]] const planner::IndexScanPlan& plan) noexcept -> bool {
    // LAB-S02.Q11:R47-BEGIN
    base::lab::missing_algorithm("S02.Q11:R47");
    // LAB-S02.Q11:R47-END
}
}  // namespace

[[maybe_unused]] StoragePlanFactory::StoragePlanFactory(
    [[maybe_unused]] QueryExecutionContext& context
)
    : context_{context} {
    // LAB-S02.Q11:R48-BEGIN
    base::lab::missing_algorithm("S02.Q11:R48");
    // LAB-S02.Q11:R48-END
}

[[maybe_unused]] auto StoragePlanFactory::create([[maybe_unused]] const planner::Plan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R49-BEGIN
    base::lab::missing_algorithm("S02.Q11:R49");
    // LAB-S02.Q11:R49-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::ValuesPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R50-BEGIN
    base::lab::missing_algorithm("S02.Q11:R50");
    // LAB-S02.Q11:R50-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::SeqScanPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R51-BEGIN
    base::lab::missing_algorithm("S02.Q11:R51");
    // LAB-S02.Q11:R51-END
}

[[maybe_unused]] auto StoragePlanFactory::create_seq_scan(
    [[maybe_unused]] const planner::SeqScanPlan& plan,
    [[maybe_unused]] const planner::ResultPlan* const result
) -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R52-BEGIN
    base::lab::missing_algorithm("S02.Q11:R52");
    // LAB-S02.Q11:R52-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::IndexScanPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R53-BEGIN
    base::lab::missing_algorithm("S02.Q11:R53");
    // LAB-S02.Q11:R53-END
}

[[maybe_unused]] auto StoragePlanFactory::create_index_scan(
    [[maybe_unused]] const planner::IndexScanPlan& plan,
    [[maybe_unused]] const planner::ResultPlan* const result
) -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R54-BEGIN
    base::lab::missing_algorithm("S02.Q11:R54");
    // LAB-S02.Q11:R54-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::ResultPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R55-BEGIN
    base::lab::missing_algorithm("S02.Q11:R55");
    // LAB-S02.Q11:R55-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::ModifyTablePlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R56-BEGIN
    base::lab::missing_algorithm("S02.Q11:R56");
    // LAB-S02.Q11:R56-END
}

template <typename PlanType, typename StateType>
[[maybe_unused]] auto StoragePlanFactory::create_unary(
    [[maybe_unused]] const PlanType& plan,
    [[maybe_unused]] base::Result<std::unique_ptr<StateType>> (*creator)(
        const PlanType&,
        std::unique_ptr<execution::PlanState>
    )
) -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R57-BEGIN
    base::lab::missing_algorithm("S02.Q11:R57");
    // LAB-S02.Q11:R57-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::SortPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R58-BEGIN
    base::lab::missing_algorithm("S02.Q11:R58");
    // LAB-S02.Q11:R58-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::LimitPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R59-BEGIN
    base::lab::missing_algorithm("S02.Q11:R59");
    // LAB-S02.Q11:R59-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::TopNPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R60-BEGIN
    base::lab::missing_algorithm("S02.Q11:R60");
    // LAB-S02.Q11:R60-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::AppendPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R61-BEGIN
    base::lab::missing_algorithm("S02.Q11:R61");
    // LAB-S02.Q11:R61-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::UniquePlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R62-BEGIN
    base::lab::missing_algorithm("S02.Q11:R62");
    // LAB-S02.Q11:R62-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::AggregatePlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R63-BEGIN
    base::lab::missing_algorithm("S02.Q11:R63");
    // LAB-S02.Q11:R63-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::SubqueryScanPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R64-BEGIN
    base::lab::missing_algorithm("S02.Q11:R64");
    // LAB-S02.Q11:R64-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::NestedLoopPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R65-BEGIN
    base::lab::missing_algorithm("S02.Q11:R65");
    // LAB-S02.Q11:R65-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::MergeJoinPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R66-BEGIN
    base::lab::missing_algorithm("S02.Q11:R66");
    // LAB-S02.Q11:R66-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::HashJoinPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R67-BEGIN
    base::lab::missing_algorithm("S02.Q11:R67");
    // LAB-S02.Q11:R67-END
}

template <typename PlanType, typename StateType>
[[maybe_unused]] auto StoragePlanFactory::create_join(
    [[maybe_unused]] const PlanType& plan,
    [[maybe_unused]] base::Result<std::unique_ptr<StateType>> (*creator)(
        const PlanType&,
        std::unique_ptr<execution::PlanState>,
        std::unique_ptr<execution::PlanState>
    )
) -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R68-BEGIN
    base::lab::missing_algorithm("S02.Q11:R68");
    // LAB-S02.Q11:R68-END
}

[[maybe_unused]] auto
StoragePlanFactory::create_node([[maybe_unused]] const planner::SubPlanEvalPlan& plan)
    -> base::Result<std::unique_ptr<execution::PlanState>> {
    // LAB-S02.Q11:R69-BEGIN
    base::lab::missing_algorithm("S02.Q11:R69");
    // LAB-S02.Q11:R69-END
}

[[maybe_unused]] auto StoragePlanFactory::affected_rows() const noexcept -> std::size_t {
    // LAB-S02.Q11:R70-BEGIN
    base::lab::missing_algorithm("S02.Q11:R70");
    // LAB-S02.Q11:R70-END
}
}  // namespace db::engine::detail
