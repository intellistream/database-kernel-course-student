#include <type_traits>

#include "db/engine/session.hpp"

template <typename Type>
concept ExposesImplementation = requires { typename Type::Impl; };

static_assert(std::is_nothrow_move_constructible_v<db::engine::Session>);
static_assert(std::is_nothrow_move_assignable_v<db::engine::Session>);
static_assert(!std::is_copy_constructible_v<db::engine::Session>);
static_assert(!ExposesImplementation<db::engine::Session>);
