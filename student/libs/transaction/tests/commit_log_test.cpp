#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/io/posix.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/xid.hpp"

#include "commit_log_format.hpp"

namespace db::transaction {
namespace {

inline constexpr std::uint64_t kSystemIdentifier = 0x1122'3344'5566'7788ULL;

auto read_file(const std::filesystem::path& path) -> std::vector<std::byte> {
    auto file = io::PosixFile::open(path);
    EXPECT_TRUE(file);
    auto size = file->size();
    EXPECT_TRUE(size);
    std::vector<std::byte> bytes(*size);
    EXPECT_TRUE(file->read_exact_at(0, bytes));
    return bytes;
}

void write_file(const std::filesystem::path& path, const base::Bytes bytes) {
    auto file = io::PosixFile::open(
        path,
        io::FileOpenOptions{
            .access = io::FileAccess::kReadWrite,
            .truncate = true,
        }
    );
    ASSERT_TRUE(file);
    ASSERT_TRUE(file->write_all_at(0, bytes));
    ASSERT_TRUE(file->sync());
}

auto is_unavailable(const base::Status& status) -> bool {
    return !status && status.error().code() == base::ErrorCode::kUnavailable;
}

auto has_inactive_contract(CommitLog& commit_log) -> testing::AssertionResult {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    if (commit_log.valid() || !commit_log.directory().empty()
        || commit_log.system_identifier() != 0U || commit_log.next_xid() != base::kInvalidXid
        || commit_log.durable_next_xid() != base::kInvalidXid || commit_log.segment_count() != 0U
        || !commit_log.segment_path(0U).empty()) {
        return testing::AssertionFailure() << "moved-from CLOG retained identity or allocation";
    }
    if (commit_log.status(base::kInvalidXid) != TransactionStatus::kCommitted
        || commit_log.status(base::Xid{1}) != TransactionStatus::kInProgress
        || commit_log.is_committed(base::Xid{1}) || commit_log.is_aborted(base::Xid{1})
        || !commit_log.is_in_progress(base::Xid{1})) {
        return testing::AssertionFailure() << "moved-from CLOG exposed a final normal-XID status";
    }
    if (!is_unavailable(commit_log.extend_to(base::Xid{2}))
        || !is_unavailable(commit_log.ensure_xid_allocated(base::Xid{1}))
        || !is_unavailable(commit_log.set_status(base::Xid{1}, TransactionStatus::kCommitted))
        || !is_unavailable(commit_log.abort_in_progress_before(base::Xid{2}))
        || !is_unavailable(commit_log.flush())) {
        return testing::AssertionFailure() << "moved-from CLOG accepted a mutating operation";
    }
    return testing::AssertionSuccess();
}

TEST(CommitLogTest, InMemoryStatusesAreFinalIdempotentAndSegmented) {
    CommitLog commit_log;
    EXPECT_EQ(commit_log.status(base::kInvalidXid), TransactionStatus::kCommitted);
    EXPECT_EQ(commit_log.status(base::Xid{7}), TransactionStatus::kInProgress);
    EXPECT_EQ(commit_log.segment_count(), 0U);

    auto invalid_allocation = commit_log.ensure_xid_allocated(base::kInvalidXid);
    ASSERT_FALSE(invalid_allocation);
    EXPECT_EQ(invalid_allocation.error().code(), base::ErrorCode::kInvalidArgument);
    auto non_final = commit_log.set_status(base::Xid{1}, TransactionStatus::kInProgress);
    ASSERT_FALSE(non_final);
    EXPECT_EQ(non_final.error().code(), base::ErrorCode::kInvalidArgument);

    ASSERT_TRUE(commit_log.set_status(base::Xid{7}, TransactionStatus::kCommitted));
    ASSERT_TRUE(commit_log.set_status(base::Xid{7}, TransactionStatus::kCommitted));
    EXPECT_TRUE(commit_log.is_committed(base::Xid{7}));
    auto changed_final = commit_log.set_status(base::Xid{7}, TransactionStatus::kAborted);
    ASSERT_FALSE(changed_final);
    EXPECT_EQ(changed_final.error().code(), base::ErrorCode::kInvariantViolation);
    EXPECT_TRUE(commit_log.is_committed(base::Xid{7}));

    const auto colliding = base::Xid{7U + 1024U};
    ASSERT_TRUE(commit_log.set_status(colliding, TransactionStatus::kAborted));
    EXPECT_TRUE(commit_log.is_aborted(colliding));
    EXPECT_TRUE(commit_log.is_committed(base::Xid{7}));

    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{kTransactionsPerCommitLogSegment - 1U}));
    EXPECT_EQ(commit_log.segment_count(), 1U);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{kTransactionsPerCommitLogSegment}));
    EXPECT_EQ(commit_log.segment_count(), 2U);
    EXPECT_EQ(commit_log.next_xid(), base::Xid{kTransactionsPerCommitLogSegment + 1U});
    ASSERT_TRUE(commit_log.flush());
    EXPECT_EQ(commit_log.durable_next_xid(), commit_log.next_xid());
}

TEST(CommitLogTest, DurableStatusesSurviveReopenAcrossSegments) {
    auto root = test_support::TestDirectory::create("commit-log-reopen");
    ASSERT_TRUE(root);
    const auto directory = root->join("xact");
    auto created = CommitLog::open(
        directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
            .create = true,
        }
    );
    ASSERT_TRUE(created);
    EXPECT_EQ(created->segment_count(), 1U);
    EXPECT_EQ(created->durable_next_xid(), kFirstNormalXid);

    const auto boundary = base::Xid{kTransactionsPerCommitLogSegment};
    ASSERT_TRUE(created->set_status(base::Xid{7}, TransactionStatus::kCommitted));
    ASSERT_TRUE(
        created->set_status(base::Xid{boundary.value() - 1U}, TransactionStatus::kCommitted)
    );
    ASSERT_TRUE(created->set_status(boundary, TransactionStatus::kAborted));
    ASSERT_TRUE(created->flush());
    EXPECT_EQ(created->durable_next_xid(), base::Xid{boundary.value() + 1U});
    EXPECT_EQ(
        std::filesystem::file_size(created->segment_path(0)),
        kCommitLogSegmentHeaderSize + kCommitLogSegmentPayloadSize
    );
    EXPECT_EQ(
        std::filesystem::file_size(created->segment_path(1)),
        kCommitLogSegmentHeaderSize + kCommitLogSegmentPayloadSize
    );

    auto reopened = CommitLog::open(
        directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
        }
    );
    ASSERT_TRUE(reopened);
    EXPECT_TRUE(reopened->is_committed(base::Xid{7}));
    EXPECT_TRUE(reopened->is_committed(base::Xid{boundary.value() - 1U}));
    EXPECT_TRUE(reopened->is_aborted(boundary));
    EXPECT_TRUE(reopened->is_in_progress(base::Xid{boundary.value() + 1U}));
    EXPECT_EQ(reopened->next_xid(), base::Xid{boundary.value() + 1U});

    auto foreign = CommitLog::open(
        directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier + 1U,
        }
    );
    ASSERT_FALSE(foreign);
    EXPECT_EQ(foreign.error().code(), base::ErrorCode::kCorruption);
    auto duplicate_create = CommitLog::open(
        directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
            .create = true,
        }
    );
    ASSERT_FALSE(duplicate_create);
    EXPECT_EQ(duplicate_create.error().code(), base::ErrorCode::kAlreadyExists);
}

TEST(CommitLogTest, DurableHeadersKeepThePublishedLittleEndianLayout) {
    auto root = test_support::TestDirectory::create("commit-log-layout");
    ASSERT_TRUE(root);
    const auto directory = root->join("xact");
    auto created = CommitLog::open(
        directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
            .create = true,
        }
    );
    ASSERT_TRUE(created);

    const auto metadata = read_file(directory / "meta");
    const auto segment = read_file(created->segment_path(0U));
    constexpr std::array k_metadata_prefix{
        std::byte{0x46},
        std::byte{0x52},
        std::byte{0x43},
        std::byte{0x4D},
        std::byte{0x45},
        std::byte{0x54},
        std::byte{0x31},
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x00},
        std::byte{0x40},
        std::byte{0x00},
    };
    constexpr std::array k_segment_prefix{
        std::byte{0x46},
        std::byte{0x52},
        std::byte{0x43},
        std::byte{0x53},
        std::byte{0x45},
        std::byte{0x47},
        std::byte{0x31},
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x00},
        std::byte{0x40},
        std::byte{0x00},
    };
    constexpr std::array k_system_identifier_bytes{
        std::byte{0x88},
        std::byte{0x77},
        std::byte{0x66},
        std::byte{0x55},
        std::byte{0x44},
        std::byte{0x33},
        std::byte{0x22},
        std::byte{0x11},
    };
    constexpr std::array k_initial_metadata_horizon{
        std::byte{0x01},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
    };

    ASSERT_GE(metadata.size(), kCommitLogMetadataSize);
    ASSERT_GE(segment.size(), kCommitLogSegmentHeaderSize);
    EXPECT_TRUE(std::equal(k_metadata_prefix.begin(), k_metadata_prefix.end(), metadata.begin()));
    EXPECT_TRUE(std::equal(k_segment_prefix.begin(), k_segment_prefix.end(), segment.begin()));
    EXPECT_TRUE(
        std::equal(
            k_system_identifier_bytes.begin(),
            k_system_identifier_bytes.end(),
            metadata.begin() + 16
        )
    );
    EXPECT_TRUE(
        std::equal(
            k_system_identifier_bytes.begin(),
            k_system_identifier_bytes.end(),
            segment.begin() + 16
        )
    );
    EXPECT_TRUE(
        std::equal(
            k_initial_metadata_horizon.begin(),
            k_initial_metadata_horizon.end(),
            metadata.begin() + 32
        )
    );
}

TEST(CommitLogTest, MoveTransfersStatusServiceAndLeavesTheSourceInert) {
    auto root = test_support::TestDirectory::create("commit-log-move");
    ASSERT_TRUE(root);
    const auto directory = root->join("xact");
    auto created = CommitLog::open(
        directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
            .create = true,
        }
    );
    ASSERT_TRUE(created);
    ASSERT_TRUE(created->set_status(base::Xid{7}, TransactionStatus::kCommitted));
    ASSERT_TRUE(created->flush());

    CommitLog source = std::move(created).value();
    CommitLog moved{std::move(source)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_TRUE(has_inactive_contract(source));
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.directory(), directory);
    EXPECT_EQ(moved.system_identifier(), kSystemIdentifier);
    EXPECT_TRUE(moved.is_committed(base::Xid{7}));

    CommitLog assigned;
    assigned = std::move(moved);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_TRUE(has_inactive_contract(moved));
    EXPECT_TRUE(assigned.valid());
    EXPECT_EQ(assigned.directory(), directory);
    EXPECT_TRUE(assigned.is_committed(base::Xid{7}));
}

TEST(CommitLogTest, ReopenRejectsChecksumAndReservedStatusCorruption) {
    auto checksum_root = test_support::TestDirectory::create("commit-log-checksum");
    ASSERT_TRUE(checksum_root);
    const auto checksum_directory = checksum_root->join("xact");
    auto checksum_log = CommitLog::open(
        checksum_directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
            .create = true,
        }
    );
    ASSERT_TRUE(checksum_log);
    auto corrupted = read_file(checksum_log->segment_path(0));
    corrupted.back() ^= std::byte{1};
    write_file(checksum_log->segment_path(0), corrupted);
    auto checksum_reopen = CommitLog::open(
        checksum_directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
        }
    );
    ASSERT_FALSE(checksum_reopen);
    EXPECT_EQ(checksum_reopen.error().code(), base::ErrorCode::kCorruption);

    auto status_root = test_support::TestDirectory::create("commit-log-status-bits");
    ASSERT_TRUE(status_root);
    const auto status_directory = status_root->join("xact");
    auto status_log = CommitLog::open(
        status_directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
            .create = true,
        }
    );
    ASSERT_TRUE(status_log);
    auto invalid_status = read_file(status_log->segment_path(0));
    invalid_status[kCommitLogSegmentHeaderSize] |= std::byte{0x03};
    ASSERT_TRUE(base::seal_crc32c(invalid_status, kCommitLogChecksumOffset));
    write_file(status_log->segment_path(0), invalid_status);
    auto status_reopen = CommitLog::open(
        status_directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
        }
    );
    ASSERT_FALSE(status_reopen);
    EXPECT_EQ(status_reopen.error().code(), base::ErrorCode::kCorruption);
}

TEST(CommitLogTest, ReopenRejectsMissingRequiredSegmentAndMetadataCorruption) {
    auto missing_root = test_support::TestDirectory::create("commit-log-missing");
    ASSERT_TRUE(missing_root);
    const auto missing_directory = missing_root->join("xact");
    auto missing_log = CommitLog::open(
        missing_directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
            .create = true,
        }
    );
    ASSERT_TRUE(missing_log);
    ASSERT_TRUE(missing_log->set_status(
        base::Xid{kTransactionsPerCommitLogSegment},
        TransactionStatus::kCommitted
    ));
    ASSERT_TRUE(missing_log->flush());
    ASSERT_TRUE(io::remove_file(missing_log->segment_path(0)));
    auto missing_reopen = CommitLog::open(
        missing_directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
        }
    );
    ASSERT_FALSE(missing_reopen);
    EXPECT_EQ(missing_reopen.error().code(), base::ErrorCode::kCorruption);

    auto metadata_root = test_support::TestDirectory::create("commit-log-meta-crc");
    ASSERT_TRUE(metadata_root);
    const auto metadata_directory = metadata_root->join("xact");
    auto metadata_log = CommitLog::open(
        metadata_directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
            .create = true,
        }
    );
    ASSERT_TRUE(metadata_log);
    auto metadata = read_file(metadata_directory / "meta");
    metadata[32] ^= std::byte{1};
    write_file(metadata_directory / "meta", metadata);
    auto metadata_reopen = CommitLog::open(
        metadata_directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
        }
    );
    ASSERT_FALSE(metadata_reopen);
    EXPECT_EQ(metadata_reopen.error().code(), base::ErrorCode::kCorruption);
}

TEST(CommitLogTest, RecoveryAbortsOnlyUnfinishedTransactionsBeforeHorizon) {
    CommitLog commit_log;
    const auto boundary = base::Xid{kTransactionsPerCommitLogSegment};
    ASSERT_TRUE(commit_log.set_status(base::Xid{1}, TransactionStatus::kCommitted));
    ASSERT_TRUE(commit_log.set_status(boundary, TransactionStatus::kCommitted));

    ASSERT_TRUE(commit_log.abort_in_progress_before(base::Xid{3}));
    EXPECT_TRUE(commit_log.is_committed(base::Xid{1}));
    EXPECT_TRUE(commit_log.is_aborted(base::Xid{2}));
    EXPECT_TRUE(commit_log.is_in_progress(base::Xid{3}));
    ASSERT_TRUE(commit_log.abort_in_progress_before(base::Xid{3}));

    ASSERT_TRUE(commit_log.abort_in_progress_before(base::Xid{boundary.value() + 2U}));
    EXPECT_TRUE(commit_log.is_committed(base::Xid{1}));
    EXPECT_TRUE(commit_log.is_aborted(base::Xid{2}));
    EXPECT_TRUE(commit_log.is_committed(boundary));
    EXPECT_TRUE(commit_log.is_aborted(base::Xid{boundary.value() + 1U}));
    EXPECT_TRUE(commit_log.is_in_progress(base::Xid{boundary.value() + 2U}));
}

TEST(CommitLogTest, ConcurrentAllocatorsInstallBoundarySegmentOnce) {
    constexpr std::size_t k_allocators = 8;
    CommitLog commit_log;
    const auto boundary = base::Xid{kTransactionsPerCommitLogSegment};
    std::atomic<bool> start{false};
    std::atomic<bool> succeeded{true};
    std::vector<std::thread> allocators;
    allocators.reserve(k_allocators);
    for (std::size_t index = 0; index < k_allocators; ++index) {
        allocators.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            if (!commit_log.ensure_xid_allocated(boundary)) {
                succeeded.store(false, std::memory_order_release);
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& allocator : allocators) {
        allocator.join();
    }

    EXPECT_TRUE(succeeded.load(std::memory_order_acquire));
    EXPECT_EQ(commit_log.segment_count(), 2U);
    EXPECT_EQ(commit_log.next_xid(), base::Xid{boundary.value() + 1U});
    ASSERT_TRUE(commit_log.flush());
    ASSERT_TRUE(commit_log.flush());
}

TEST(CommitLogTest, ConcurrentUpdatesAndFlushesRemainDurable) {
    constexpr std::size_t k_writers = 4;
    constexpr std::size_t k_xids_per_writer = 2000;
    auto root = test_support::TestDirectory::create("commit-log-concurrent");
    ASSERT_TRUE(root);
    const auto directory = root->join("xact");
    auto created = CommitLog::open(
        directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
            .create = true,
        }
    );
    ASSERT_TRUE(created);
    auto commit_log = std::make_shared<CommitLog>(std::move(*created));
    std::atomic<std::size_t> running{k_writers};
    std::atomic<bool> succeeded{true};
    std::vector<std::thread> writers;
    writers.reserve(k_writers);
    for (std::size_t writer = 0; writer < k_writers; ++writer) {
        writers.emplace_back([&, writer] {
            const auto first = writer * k_xids_per_writer + 1U;
            for (std::size_t offset = 0; offset < k_xids_per_writer; ++offset) {
                const auto value = first + offset;
                const auto final_status =
                    value % 2U == 0 ? TransactionStatus::kCommitted : TransactionStatus::kAborted;
                if (!commit_log
                         ->set_status(base::Xid{static_cast<std::uint32_t>(value)}, final_status)) {
                    succeeded.store(false, std::memory_order_relaxed);
                    break;
                }
            }
            running.fetch_sub(1, std::memory_order_release);
        });
    }
    while (running.load(std::memory_order_acquire) != 0) {
        if (!commit_log->flush()) {
            succeeded.store(false, std::memory_order_relaxed);
            break;
        }
        std::this_thread::yield();
    }
    for (auto& writer : writers) {
        writer.join();
    }
    ASSERT_TRUE(succeeded.load(std::memory_order_relaxed));
    ASSERT_TRUE(commit_log->flush());
    commit_log.reset();

    auto reopened = CommitLog::open(
        directory,
        CommitLogOpenOptions{
            .system_identifier = kSystemIdentifier,
        }
    );
    ASSERT_TRUE(reopened);
    for (std::size_t value = 1; value <= k_writers * k_xids_per_writer; ++value) {
        const auto expected =
            value % 2U == 0 ? TransactionStatus::kCommitted : TransactionStatus::kAborted;
        EXPECT_EQ(reopened->status(base::Xid{static_cast<std::uint32_t>(value)}), expected);
    }
}

}  // namespace
}  // namespace db::transaction
