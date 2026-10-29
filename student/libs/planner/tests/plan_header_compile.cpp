#include <type_traits>
#include <utility>

#include "db/planner/explain.hpp"
#include "db/planner/plan.hpp"

static_assert(!std::is_copy_constructible_v<db::planner::Plan>);
static_assert(std::is_nothrow_move_constructible_v<db::planner::Plan>);
static_assert(!std::is_copy_constructible_v<db::planner::PlanExpression>);
static_assert(std::is_nothrow_move_constructible_v<db::planner::PlanExpression>);
static_assert(!std::is_copy_constructible_v<db::planner::ParameterNamespace>);
static_assert(std::is_nothrow_move_constructible_v<db::planner::ParameterNamespace>);
static_assert(std::is_nothrow_move_assignable_v<db::planner::ParameterNamespace>);
static_assert(std::is_trivially_copyable_v<db::planner::PlanSourceSpan>);
static_assert(std::is_trivially_copyable_v<db::planner::ExternalParameterNumber>);
static_assert(std::is_same_v<
              decltype(std::declval<const db::planner::PlannedStatement&>().root()),
              const db::planner::Plan&>);
static_assert(std::is_same_v<
              decltype(std::declval<const db::planner::PlannedStatement&>().parameter_namespace()),
              const db::planner::ParameterNamespace&>);
