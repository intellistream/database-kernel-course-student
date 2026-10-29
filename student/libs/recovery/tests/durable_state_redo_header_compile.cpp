#include <type_traits>

#include "db/recovery/durable_state_redo.hpp"

static_assert(std::is_move_constructible_v<db::recovery::DurableStateReplayer>);
static_assert(std::is_nothrow_move_constructible_v<db::recovery::DurableStateReplayer>);
static_assert(std::is_nothrow_move_assignable_v<db::recovery::DurableStateReplayer>);
static_assert(db::recovery::DurableStateReplaySeed{} == db::recovery::DurableStateReplaySeed{});
