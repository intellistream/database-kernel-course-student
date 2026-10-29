#include <type_traits>

#include "db/transaction/proc_array.hpp"

static_assert(!std::is_copy_constructible_v<db::transaction::ProcArray>);
static_assert(!std::is_move_constructible_v<db::transaction::ProcArray>);
static_assert(!std::is_copy_constructible_v<db::transaction::ProcSlotLease>);
static_assert(std::is_nothrow_move_constructible_v<db::transaction::ProcSlotLease>);
