#include <type_traits>

#include "db/engine/database.hpp"

template <typename Type>
concept ExposesImplementation = requires { typename Type::Impl; };

static_assert(std::is_move_constructible_v<db::engine::Database>);
static_assert(!std::is_move_assignable_v<db::engine::Database>);
static_assert(!std::is_copy_constructible_v<db::engine::Database>);
static_assert(!ExposesImplementation<db::engine::Database>);
