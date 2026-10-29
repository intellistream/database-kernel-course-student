#pragma once

#include <chrono>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/completion_journal.hpp"

namespace db::wal {
class WalManager;
}  // namespace db::wal

namespace db::recovery {

// Composition-layer adapter from transaction's completion journal to the WAL
// transaction resource manager. The borrowed WalManager must outlive this
// adapter and every call through it.
class TransactionCompletionWalAdapter final : public transaction::CompletionJournalPort {
public:
    explicit TransactionCompletionWalAdapter(wal::WalManager& manager) noexcept;

    auto append_completion(base::Xid xid, transaction::CompletionOutcome outcome)
        -> base::Result<base::Lsn> override;
    auto flush_through(base::Lsn end_lsn, std::chrono::microseconds group_delay)
        -> base::Status override;

private:
    wal::WalManager* manager_;
};

}  // namespace db::recovery
