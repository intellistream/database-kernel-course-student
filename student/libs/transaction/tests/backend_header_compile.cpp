#include <type_traits>

#include "db/transaction/backend.hpp"

static_assert(std::is_default_constructible_v<db::transaction::BackendLocalState>);
static_assert(!std::is_copy_constructible_v<db::transaction::TransactionBackend>);
static_assert(!std::is_move_constructible_v<db::transaction::TransactionBackend>);
