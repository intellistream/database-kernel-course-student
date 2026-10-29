#include <type_traits>
#include <utility>

#include "db/planner/join_strategy.hpp"

static_assert(std::is_copy_constructible_v<db::planner::JoinStrategyResult>);
static_assert(std::is_move_constructible_v<db::planner::JoinStrategyResult>);
static_assert(std::is_same_v<
              decltype(db::planner::choose_small_inner_join_strategies(
                  std::declval<std::span<const db::planner::BaseRelationPathList>>(),
                  std::declval<std::span<const db::planner::JoinQualification>>(),
                  std::declval<std::span<const db::planner::PreprocessedExpression* const>>(),
                  std::declval<db::planner::ParameterNamespace&>()
              )),
              db::base::Result<db::planner::JoinStrategyResult>>);
