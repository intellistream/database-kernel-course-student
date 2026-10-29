#include <type_traits>

#include "db/engine/result.hpp"

static_assert(std::is_copy_constructible_v<db::engine::ResultSchema>);
static_assert(std::is_copy_constructible_v<db::engine::ResultRow>);
static_assert(std::is_copy_constructible_v<db::engine::ResultSet>);
static_assert(std::is_move_constructible_v<db::engine::ResultSet>);
