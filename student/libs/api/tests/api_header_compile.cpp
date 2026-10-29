#include <type_traits>

#include "db/api/api.hpp"

static_assert(std::is_move_constructible_v<db::api::Database>);
static_assert(!std::is_move_assignable_v<db::api::Database>);
static_assert(std::is_move_constructible_v<db::api::Session>);
static_assert(std::is_move_constructible_v<db::api::PreparedStatement>);
static_assert(std::is_copy_constructible_v<db::api::ResultSet>);
