#include <type_traits>

#include "db/course/course.hpp"

static_assert(std::is_copy_constructible_v<db::course::foundations::ResultSet>);
static_assert(std::is_move_constructible_v<db::course::storage::PinGuard>);
