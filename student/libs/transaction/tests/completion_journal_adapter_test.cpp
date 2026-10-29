#include <chrono>
#include <stdexcept>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/transaction/completion_journal.hpp"

#include "completion_journal_adapter.hpp"

namespace db::transaction::detail {
namespace {

class ThrowingCompletionJournal final : public CompletionJournalPort {
public:
    auto
    append_completion([[maybe_unused]] base::Xid xid, [[maybe_unused]] CompletionOutcome outcome)
        -> base::Result<base::Lsn> override {
        throw std::runtime_error{"append failure"};
    }

    auto flush_through(
        [[maybe_unused]] base::Lsn end_lsn,
        [[maybe_unused]] std::chrono::microseconds group_delay
    ) -> base::Status override {
        throw 7;
    }
};

TEST(CompletionJournalAdapterTest, ConvertsPortExceptionsAtTheNarrowBoundary) {
    ThrowingCompletionJournal journal;
    auto appended = append_completion_journal(
        journal,
        base::Xid{1},
        CompletionOutcome::kCommit,
        "append test journal"
    );
    ASSERT_FALSE(appended);
    EXPECT_EQ(appended.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(appended.error().to_string().find("append failure"), std::string::npos);

    auto flushed = flush_completion_journal(
        journal,
        base::Lsn{8},
        std::chrono::microseconds::zero(),
        "flush test journal"
    );
    ASSERT_FALSE(flushed);
    EXPECT_EQ(flushed.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(flushed.error().to_string().find("non-standard"), std::string::npos);
}

}  // namespace
}  // namespace db::transaction::detail
