#include <type_traits>

#include "db/course/query.hpp"

static_assert(std::is_same_v<db::course::query::Plan, db::planner::Plan>);
static_assert(std::is_same_v<db::course::query::Tuple, db::execution::Tuple>);
static_assert(std::is_same_v<db::course::query::CoercionForm, db::sql::BoundCastForm>);
static_assert(db::course::query::CoercionPath{}.is_relabel());
