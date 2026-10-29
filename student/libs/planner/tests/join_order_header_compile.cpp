#include <type_traits>
#include <utility>

#include "db/planner/join_order.hpp"

static_assert(std::is_move_constructible_v<db::planner::SmallInnerJoinPathResult>);
static_assert(std::is_copy_constructible_v<db::planner::SmallInnerJoinPathResult>);
static_assert(!std::is_copy_constructible_v<db::planner::PreprocessedTableReference>);
static_assert(std::is_same_v<
              decltype(db::planner::collect_small_inner_join_input(
                  std::declval<const db::planner::PreprocessedTableReference&>()
              )),
              db::base::Result<db::planner::SmallInnerJoinInputView>>);
static_assert(std::is_same_v<
              decltype(db::planner::build_small_inner_join_paths(
                  std::declval<const db::planner::MetadataPort&>(),
                  std::declval<const db::planner::SmallInnerJoinInputView&>(),
                  std::declval<const db::planner::ParameterNamespace&>()
              )),
              db::base::Result<db::planner::SmallInnerJoinPathResult>>);
static_assert(std::is_same_v<
              decltype(db::planner::choose_small_inner_join_order(
                  std::declval<std::span<const db::planner::BaseRelationPathList>>(),
                  std::declval<std::span<const db::planner::JoinQualification>>()
              )),
              db::base::Result<db::planner::JoinOrderSearchResult>>);
