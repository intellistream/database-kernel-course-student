#include <cstdint>

#include <gtest/gtest.h>

#include "db/transaction/commit_log.hpp"

#include "commit_log_segment.hpp"

namespace db::transaction {
namespace {

TEST(CommitLogSegmentTest, OwnsPackedMutationAndGenerationUnderItsPageLatch) {
    CommitLogSegment segment{7};

    EXPECT_EQ(segment.status(5), TransactionStatus::kInProgress);
    EXPECT_EQ(segment.publish(5, TransactionStatus::kCommitted), CommitLogStatusWrite::kChanged);
    EXPECT_EQ(segment.publish(5, TransactionStatus::kCommitted), CommitLogStatusWrite::kUnchanged);
    EXPECT_EQ(segment.publish(5, TransactionStatus::kAborted), CommitLogStatusWrite::kConflict);

    const auto published = segment.snapshot();
    EXPECT_EQ(published.generation, 8U);
    EXPECT_FALSE(segment.has_changed_since(published.generation));

    EXPECT_TRUE(segment.abort_in_progress(6, 9));
    EXPECT_EQ(segment.status(5), TransactionStatus::kCommitted);
    EXPECT_EQ(segment.status(6), TransactionStatus::kAborted);
    EXPECT_TRUE(segment.has_changed_since(published.generation));
    EXPECT_EQ(segment.snapshot().generation, 9U);
}

}  // namespace
}  // namespace db::transaction
