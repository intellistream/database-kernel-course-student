#include <type_traits>

#include "db/course/foundations.hpp"

static_assert(std::is_same_v<db::course::foundations::ResultSet, db::api::ResultSet>);
static_assert(std::is_same_v<db::course::foundations::Row, db::execution::Tuple>);
