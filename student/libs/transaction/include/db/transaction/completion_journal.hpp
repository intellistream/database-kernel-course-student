#pragma once

#include <chrono>
#include <cstdint>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::transaction {

enum class CompletionOutcome : std::uint8_t {
    kCommit,
    kAbort,
};

// Consumer-owned recovery-journal boundary. Transaction controls publication
// order and group-delay policy without depending on a concrete WAL service or
// its record representation. Escaped implementation exceptions are converted
// to structured transaction errors at the manager boundary; no exception may
// cross a transaction or recovery entry point.
class CompletionJournalPort {
public:
    CompletionJournalPort() = default;
    CompletionJournalPort(const CompletionJournalPort&) = delete;
    auto operator=(const CompletionJournalPort&) -> CompletionJournalPort& = delete;
    CompletionJournalPort(CompletionJournalPort&&) = delete;
    auto operator=(CompletionJournalPort&&) -> CompletionJournalPort& = delete;
    virtual ~CompletionJournalPort() = default;

    // Returns the exclusive end position of the appended completion record.
    // Append alone need not make the record durable.
    virtual auto append_completion(base::Xid xid, CompletionOutcome outcome)
        -> base::Result<base::Lsn> = 0;

    // Makes the journal durable through end_lsn. Implementations may use the
    // bounded delay to combine concurrent commit flushes.
    virtual auto flush_through(base::Lsn end_lsn, std::chrono::microseconds group_delay)
        -> base::Status = 0;
};

}  // namespace db::transaction
