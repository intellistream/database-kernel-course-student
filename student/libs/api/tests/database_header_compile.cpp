#include <type_traits>

#include "db/api/database.hpp"

template <typename Type>
concept ExposesImplementation = requires { typename Type::Impl; };

static_assert(std::is_move_constructible_v<db::api::Database>);
static_assert(!std::is_move_assignable_v<db::api::Database>);
static_assert(!std::is_copy_constructible_v<db::api::Database>);
static_assert(std::is_move_constructible_v<db::api::PreparedStatement>);
static_assert(!std::is_copy_constructible_v<db::api::PreparedStatement>);
static_assert(!ExposesImplementation<db::api::Database>);
static_assert(!ExposesImplementation<db::api::PreparedStatement>);
