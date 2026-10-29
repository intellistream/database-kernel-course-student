#pragma once

#include <chrono>
#include <string_view>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/completion_journal.hpp"

namespace db::transaction::detail {

// Narrow exception-converting adapter around the consumer-owned journal port.
// Transaction ordering remains with TransactionManager; no implementation
// exception may cross this boundary.
auto append_completion_journal(
    CompletionJournalPort& journal,
    base::Xid xid,
    CompletionOutcome outcome,
    std::string_view operation
) -> base::Result<base::Lsn>;
auto flush_completion_journal(
    CompletionJournalPort& journal,
    base::Lsn end_lsn,
    std::chrono::microseconds group_delay,
    std::string_view operation
) -> base::Status;

}  // namespace db::transaction::detail
