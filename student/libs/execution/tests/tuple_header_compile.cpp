#include <type_traits>

#include "db/execution/tuple.hpp"

static_assert(std::is_copy_constructible_v<db::execution::TupleDescriptorHandle>);
static_assert(std::is_copy_constructible_v<db::execution::Tuple>);
static_assert(!std::is_copy_constructible_v<db::execution::TupleSlot>);
static_assert(std::is_nothrow_move_constructible_v<db::execution::TupleSlot>);
static_assert(std::is_nothrow_move_assignable_v<db::execution::TupleSlot>);
