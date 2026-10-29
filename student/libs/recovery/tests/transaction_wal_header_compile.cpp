#include <type_traits>

#include "db/recovery/transaction_wal.hpp"

namespace {

static_assert(std::is_final_v<db::recovery::TransactionCompletionWalAdapter>);
static_assert(std::is_base_of_v<
              db::transaction::CompletionJournalPort,
              db::recovery::TransactionCompletionWalAdapter>);

}  // namespace
