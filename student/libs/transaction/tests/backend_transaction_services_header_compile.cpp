#include <type_traits>

#include "backend_transaction_services.hpp"

static_assert(!std::is_copy_constructible_v<db::transaction::detail::BackendTransactionServices>);
static_assert(!std::is_move_constructible_v<db::transaction::detail::BackendTransactionServices>);
