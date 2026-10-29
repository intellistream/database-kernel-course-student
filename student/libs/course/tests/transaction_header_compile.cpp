#include <type_traits>

#include "db/course/transaction.hpp"

static_assert(std::is_same_v<
              db::course::transaction::TransactionState,
              db::transaction::TransactionBlockState>);
static_assert(std::is_move_constructible_v<db::course::transaction::ProcSlotLease>);
