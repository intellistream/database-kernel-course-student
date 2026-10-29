#include <type_traits>
#include <utility>

#include "db/planner/preprocess.hpp"

static_assert(!std::is_copy_constructible_v<db::planner::PreprocessedExpression>);
static_assert(std::is_nothrow_move_constructible_v<db::planner::PreprocessedExpression>);
static_assert(!std::is_copy_constructible_v<db::planner::PreprocessedTableReference>);
static_assert(std::is_nothrow_move_constructible_v<db::planner::PreprocessedTableReference>);
static_assert(!std::is_copy_constructible_v<db::planner::PreprocessedQuery>);
static_assert(std::is_nothrow_move_constructible_v<db::planner::PreprocessedQuery>);
static_assert(!std::is_copy_constructible_v<db::planner::PreprocessedStatement>);
static_assert(std::is_nothrow_move_constructible_v<db::planner::PreprocessedStatement>);
