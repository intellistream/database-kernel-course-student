#include "completion_journal_adapter.hpp"

namespace {

[[maybe_unused]] void
compile_completion_journal_adapter_header(db::transaction::CompletionJournalPort& journal) {
    const auto appended = db::transaction::detail::append_completion_journal(
        journal,
        db::base::Xid{1},
        db::transaction::CompletionOutcome::kCommit,
        "compile"
    );
    (void)appended;
}

}  // namespace
