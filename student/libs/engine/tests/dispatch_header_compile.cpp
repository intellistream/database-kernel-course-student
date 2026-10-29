#include <type_traits>

#include "db/engine/dispatch.hpp"

template <typename Type>
concept ExposesImplementation = requires { typename Type::Impl; };

static_assert(std::is_move_constructible_v<db::engine::PreparedStatement>);
static_assert(std::is_move_assignable_v<db::engine::PreparedStatement>);
static_assert(!std::is_copy_constructible_v<db::engine::PreparedStatement>);
static_assert(std::is_move_constructible_v<db::engine::DispatchPlan>);
static_assert(!std::is_copy_constructible_v<db::engine::DispatchPlan>);
static_assert(!ExposesImplementation<db::engine::PreparedStatement>);
static_assert(!ExposesImplementation<db::engine::DispatchPlan>);
