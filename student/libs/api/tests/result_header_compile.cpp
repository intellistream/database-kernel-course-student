#include <type_traits>
#include <utility>

#include "db/api/result.hpp"

static_assert(std::is_copy_constructible_v<db::api::ResultSchema>);
static_assert(std::is_copy_constructible_v<db::api::ResultRow>);
static_assert(std::is_copy_constructible_v<db::api::ResultSet>);
static_assert(std::is_move_constructible_v<db::api::ResultSet>);

template <typename Result>
concept ConsumableResult = requires(Result&& result) { std::forward<Result>(result).into_data(); };

static_assert(ConsumableResult<db::api::ResultSet>);
static_assert(!ConsumableResult<db::api::ResultSet&>);
static_assert(!ConsumableResult<const db::api::ResultSet>);
static_assert(!ConsumableResult<const db::api::ResultSet&>);
static_assert(std::is_nothrow_move_constructible_v<db::api::ResultSetData>);
