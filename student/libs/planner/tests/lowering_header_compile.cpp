#include <type_traits>
#include <utility>

#include "db/planner/lowering.hpp"

namespace db::sql {
struct BoundParameterSlot;
struct BoundQuery;
struct BoundStatement;
}  // namespace db::sql

static_assert(!std::is_copy_constructible_v<db::planner::LoweredSelectPlan>);
static_assert(std::is_nothrow_move_constructible_v<db::planner::LoweredSelectPlan>);
static_assert(std::is_same_v<
              decltype(db::planner::lower_preprocessed_select(
                  std::declval<db::planner::PreprocessedQuery>(),
                  std::declval<const db::planner::MetadataPort&>(),
                  std::declval<db::planner::ParameterNamespace>()
              )),
              db::base::Result<db::planner::LoweredSelectPlan>>);
static_assert(std::is_same_v<
              decltype(db::planner::assemble_planned_select(
                  std::declval<db::planner::LoweredSelectPlan>()
              )),
              db::base::Result<db::planner::PlannedStatement>>);
static_assert(std::is_same_v<
              decltype(db::planner::lower_preprocessed_statement(
                  std::declval<db::planner::PreprocessedStatement>(),
                  std::declval<const db::planner::MetadataPort&>(),
                  std::declval<db::planner::ParameterNamespace>()
              )),
              db::base::Result<db::planner::PlannedStatement>>);
static_assert(std::is_same_v<
              decltype(&db::planner::lower_bound_statement),
              db::base::Result<db::planner::PlannedStatement> (*)(
                  db::sql::BoundStatement,
                  std::span<const db::sql::BoundParameterSlot>,
                  const db::planner::MetadataPort&
              )>);
static_assert(std::is_same_v<
              decltype(&db::planner::lower_bound_select),
              db::base::Result<db::planner::LoweredSelectPlan> (*)(
                  db::sql::BoundQuery,
                  std::span<const db::sql::BoundParameterSlot>,
                  const db::planner::MetadataPort&
              )>);
