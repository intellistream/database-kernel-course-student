#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <semaphore>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/format.hpp"
#include "db/wal/ring_buffer.hpp"
#include "db/wal/segment_storage.hpp"
#include "db/wal/writer.hpp"

#include "wal_fault_port.hpp"

namespace db::wal {
namespace {

using namespace std::chrono_literals;

inline constexpr std::uint64_t kSystemIdentifier = 0x1122'3344'5566'7788ULL;

auto storage_options(const bool create, std::shared_ptr<WalStorageFaultPort> fault_port = nullptr)
    -> WalSegmentStorageOptions {
    return WalSegmentStorageOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = 1,
        .retention_lsn = base::kInvalidLsn,
        .create = create,
        .fault_port = std::move(fault_port),
    };
}

auto patterned_bytes(const std::size_t size, const std::uint8_t seed = 7)
    -> std::vector<std::byte> {
    std::vector<std::byte> bytes(size);
    for (std::size_t index = 0; index < size; ++index) {
        bytes[index] = static_cast<std::byte>((index * 29U + seed) & 0xFFU);
    }
    return bytes;
}

auto create_writer(
    const std::filesystem::path& directory,
    const std::size_t ring_pages = 4,
    std::shared_ptr<WalStorageFaultPort> fault_port = nullptr
) -> base::Result<WalWriter> {
    auto storage = WalSegmentStorage::open(directory, storage_options(true, std::move(fault_port)));
    if (!storage) {
        return base::fail(std::move(storage).error());
    }
    return WalWriter::create(std::move(storage).value(), ring_pages);
}

auto reserve(WalWriter& writer, const std::size_t size) -> WalReservation {
    auto outcome = writer.try_reserve(size);
    EXPECT_TRUE(outcome);
    EXPECT_TRUE(std::holds_alternative<WalReservation>(outcome.value()));
    return std::get<WalReservation>(outcome.value());
}

class BlockingRecyclePort final : public WalStorageFaultPort {
public:
    auto before_operation(const WalStorageFaultPoint point) -> base::Status override {
        if (point != WalStorageFaultPoint::kRecycle) {
            return {};
        }
        entered_.release();
        resume_.acquire();
        return {};
    }

    auto wait_until_entered(const std::chrono::milliseconds timeout) -> bool {
        return entered_.try_acquire_for(timeout);
    }

    void release() {
        resume_.release();
    }

private:
    std::binary_semaphore entered_{0};
    std::binary_semaphore resume_{0};
};

TEST(WalWriterTest, BootstrapsGuardPageAndSeparatesWrittenFromFlushedProgress) {
    auto root = test_support::TestDirectory::create("wal-writer-progress");
    ASSERT_TRUE(root);
    auto writer = create_writer(root->join("wal"));
    ASSERT_TRUE(writer);
    EXPECT_EQ(writer->written_lsn(), kInitialWalRingLsn);
    EXPECT_EQ(writer->flushed_lsn(), kInitialWalRingLsn);
    EXPECT_EQ(writer->successful_sync_count(), 0U);

    const auto payload = patterned_bytes(kWalRecordHeaderSize);
    const auto reservation = reserve(writer.value(), payload.size());
    ASSERT_TRUE(writer->copy_in(reservation, payload));
    ASSERT_TRUE(writer->write_through(reservation.end_lsn));
    EXPECT_EQ(writer->written_lsn(), reservation.end_lsn);
    EXPECT_EQ(writer->flushed_lsn(), kInitialWalRingLsn);
    EXPECT_EQ(writer->successful_sync_count(), 0U);

    ASSERT_TRUE(writer->flush_through(reservation.end_lsn));
    EXPECT_EQ(writer->flushed_lsn(), reservation.end_lsn);
    EXPECT_EQ(writer->successful_sync_count(), 1U);
    ASSERT_TRUE(writer->flush_through(reservation.end_lsn));
    EXPECT_EQ(writer->successful_sync_count(), 1U);
}

TEST(WalWriterTest, PersistsFramedBytesAndResumesAtAValidatedTail) {
    auto root = test_support::TestDirectory::create("wal-writer-reopen");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    const auto payload = patterned_bytes(kWalRecordHeaderSize, 37);
    base::Lsn first_end{base::kInvalidLsn};
    {
        auto writer = create_writer(directory);
        ASSERT_TRUE(writer);
        const auto reservation = reserve(writer.value(), payload.size());
        first_end = reservation.end_lsn;
        ASSERT_TRUE(writer->copy_in(reservation, payload));
        ASSERT_TRUE(writer->flush_through(first_end));
    }

    auto storage = WalSegmentStorage::open(directory, storage_options(false));
    ASSERT_TRUE(storage);
    EXPECT_EQ(storage->logical_size(), first_end.value());
    std::array<std::byte, kWalPageSize> guard{};
    ASSERT_TRUE(storage->read_exact_at(0, guard));
    EXPECT_TRUE(std::all_of(guard.begin(), guard.end(), [](const std::byte byte) {
        return byte == std::byte{0};
    }));
    WalPageHeaderBytes header{};
    ASSERT_TRUE(storage->read_exact_at(kInitialWalRingLsn.value(), header));
    EXPECT_TRUE(base::verify_crc32c(header, kWalPageChecksumOffset));
    const auto decoded = decode_page_header(header);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->page_start_lsn, kInitialWalRingLsn);
    std::array<std::byte, kWalRecordHeaderSize> stored_payload{};
    ASSERT_TRUE(
        storage->read_exact_at(kInitialWalRingLsn.value() + kWalPageHeaderSize, stored_payload)
    );
    EXPECT_TRUE(std::equal(stored_payload.begin(), stored_payload.end(), payload.begin()));

    auto resumed = WalWriter::create(std::move(storage).value(), 4);
    ASSERT_TRUE(resumed);
    EXPECT_EQ(resumed->written_lsn(), first_end);
    EXPECT_EQ(resumed->flushed_lsn(), first_end);
    const auto next_payload = patterned_bytes(kWalRecordHeaderSize, 41);
    const auto next = reserve(resumed.value(), next_payload.size());
    EXPECT_EQ(next.start_lsn, first_end);
    ASSERT_TRUE(resumed->copy_in(next, next_payload));
    ASSERT_TRUE(resumed->flush_through(next.end_lsn));
}

TEST(WalWriterTest, WaitsForReservedPayloadBeforeWriting) {
    auto root = test_support::TestDirectory::create("wal-writer-waits");
    ASSERT_TRUE(root);
    auto writer = create_writer(root->join("wal"));
    ASSERT_TRUE(writer);
    const auto payload = patterned_bytes(kWalRecordHeaderSize);
    const auto reservation = reserve(writer.value(), payload.size());

    auto write = std::async(std::launch::async, [&writer, reservation] {
        return writer->write_through(reservation.end_lsn);
    });
    EXPECT_EQ(write.wait_for(40ms), std::future_status::timeout);
    ASSERT_TRUE(writer->copy_in(reservation, payload));
    ASSERT_EQ(write.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(write.get());
    EXPECT_EQ(writer->written_lsn(), reservation.end_lsn);
}

TEST(WalWriterTest, ConcurrentCommitFlushersShareOneLeaderSync) {
    auto root = test_support::TestDirectory::create("wal-writer-group");
    ASSERT_TRUE(root);
    auto writer = create_writer(root->join("wal"));
    ASSERT_TRUE(writer);
    const auto payload = patterned_bytes(kWalRecordHeaderSize);
    const auto reservation = reserve(writer.value(), payload.size());
    ASSERT_TRUE(writer->copy_in(reservation, payload));

    constexpr std::size_t thread_count = 12;
    std::barrier ready{static_cast<std::ptrdiff_t>(thread_count + 1U)};
    std::array<std::atomic<bool>, thread_count> succeeded{};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (std::size_t index = 0; index < thread_count; ++index) {
        threads.emplace_back([&writer, &ready, &succeeded, reservation, index] {
            ready.arrive_and_wait();
            const auto status = writer->flush_commit(reservation.end_lsn, 100ms);
            succeeded.at(index).store(status.has_value(), std::memory_order_relaxed);
        });
    }
    ready.arrive_and_wait();
    for (auto& thread : threads) {
        thread.join();
    }
    for (const auto& result : succeeded) {
        EXPECT_TRUE(result.load(std::memory_order_relaxed));
    }
    EXPECT_EQ(writer->flushed_lsn(), reservation.end_lsn);
    EXPECT_EQ(writer->successful_sync_count(), 1U);
}

TEST(WalWriterTest, FlushLeaderWidensToLatestCompletedPrefix) {
    auto root = test_support::TestDirectory::create("wal-writer-widen");
    ASSERT_TRUE(root);
    auto writer = create_writer(root->join("wal"));
    ASSERT_TRUE(writer);
    const auto first_payload = patterned_bytes(kWalRecordHeaderSize, 3);
    const auto second_payload = patterned_bytes(kWalRecordHeaderSize, 5);
    const auto first = reserve(writer.value(), first_payload.size());
    const auto second = reserve(writer.value(), second_payload.size());
    ASSERT_TRUE(writer->copy_in(first, first_payload));
    ASSERT_TRUE(writer->copy_in(second, second_payload));

    ASSERT_TRUE(writer->flush_through(first.end_lsn));
    EXPECT_EQ(writer->written_lsn(), second.end_lsn);
    EXPECT_EQ(writer->flushed_lsn(), second.end_lsn);
    EXPECT_EQ(writer->successful_sync_count(), 1U);
}

TEST(WalWriterTest, FlushNeverCrossesAnIncompleteLaterReservation) {
    auto root = test_support::TestDirectory::create("wal-writer-safe-prefix");
    ASSERT_TRUE(root);
    auto writer = create_writer(root->join("wal"));
    ASSERT_TRUE(writer);
    const auto first_payload = patterned_bytes(kWalRecordHeaderSize, 11);
    const auto second_payload = patterned_bytes(kWalRecordHeaderSize, 13);
    const auto first = reserve(writer.value(), first_payload.size());
    const auto second = reserve(writer.value(), second_payload.size());
    ASSERT_TRUE(writer->copy_in(first, first_payload));

    ASSERT_TRUE(writer->flush_through(first.end_lsn));
    EXPECT_EQ(writer->written_lsn(), first.end_lsn);
    EXPECT_EQ(writer->flushed_lsn(), first.end_lsn);
    ASSERT_TRUE(writer->copy_in(second, second_payload));
    ASSERT_TRUE(writer->flush_through(second.end_lsn));
    EXPECT_EQ(writer->flushed_lsn(), second.end_lsn);
    EXPECT_EQ(writer->successful_sync_count(), 2U);
}

TEST(WalWriterTest, WritesAndReleasesOneOversizedBatchAsAWhole) {
    auto root = test_support::TestDirectory::create("wal-writer-oversized");
    ASSERT_TRUE(root);
    auto writer = create_writer(root->join("wal"), 1);
    ASSERT_TRUE(writer);
    const auto payload = patterned_bytes(kWalPageSize + 8U);
    const auto reservation = reserve(writer.value(), payload.size());
    ASSERT_TRUE(reservation.oversized);
    ASSERT_TRUE(writer->copy_in(reservation, payload));
    ASSERT_TRUE(writer->write_through(reservation.end_lsn));
    EXPECT_EQ(writer->written_lsn(), reservation.end_lsn);

    const auto next = reserve(writer.value(), kWalRecordHeaderSize);
    EXPECT_FALSE(next.oversized);
}

TEST(WalWriterTest, InjectedWriteFailureKeepsRingAndWrittenFrontierRetryable) {
    auto root = test_support::TestDirectory::create("wal-writer-failed-write");
    ASSERT_TRUE(root);
    auto faults = std::make_shared<test::OneShotWalStorageFaultPort>();
    auto writer = create_writer(root->join("wal"), 4, faults);
    ASSERT_TRUE(writer);
    const auto payload = patterned_bytes(kWalRecordHeaderSize);
    const auto reservation = reserve(writer.value(), payload.size());
    ASSERT_TRUE(writer->copy_in(reservation, payload));

    faults->arm(WalStorageFaultPoint::kWrite);
    const auto failed = writer->write_through(reservation.end_lsn);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(writer->written_lsn(), kInitialWalRingLsn);
    EXPECT_EQ(writer->flushed_lsn(), kInitialWalRingLsn);

    ASSERT_TRUE(writer->write_through(reservation.end_lsn));
    EXPECT_EQ(writer->written_lsn(), reservation.end_lsn);
    ASSERT_TRUE(writer->flush_through(reservation.end_lsn));
    EXPECT_EQ(writer->flushed_lsn(), reservation.end_lsn);
}

TEST(WalWriterTest, InjectedSyncFailureKeepsWrittenAheadOfFlushedUntilRetry) {
    auto root = test_support::TestDirectory::create("wal-writer-failed-sync");
    ASSERT_TRUE(root);
    auto faults = std::make_shared<test::OneShotWalStorageFaultPort>();
    auto writer = create_writer(root->join("wal"), 4, faults);
    ASSERT_TRUE(writer);
    const auto payload = patterned_bytes(kWalRecordHeaderSize);
    const auto reservation = reserve(writer.value(), payload.size());
    ASSERT_TRUE(writer->copy_in(reservation, payload));
    ASSERT_TRUE(writer->write_through(reservation.end_lsn));

    faults->arm(WalStorageFaultPoint::kSync);
    const auto failed = writer->flush_through(reservation.end_lsn);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(writer->written_lsn(), reservation.end_lsn);
    EXPECT_EQ(writer->flushed_lsn(), kInitialWalRingLsn);
    EXPECT_EQ(writer->successful_sync_count(), 0U);

    ASSERT_TRUE(writer->flush_through(reservation.end_lsn));
    EXPECT_EQ(writer->written_lsn(), reservation.end_lsn);
    EXPECT_EQ(writer->flushed_lsn(), reservation.end_lsn);
    EXPECT_EQ(writer->successful_sync_count(), 1U);
}

TEST(WalWriterTest, RetentionIoOwnerWakesAWaitingFlushFollower) {
    auto root = test_support::TestDirectory::create("wal-writer-retention-wakeup");
    ASSERT_TRUE(root);
    auto faults = std::make_shared<BlockingRecyclePort>();
    auto writer = create_writer(root->join("wal"), 4, faults);
    ASSERT_TRUE(writer);

    const auto payload = patterned_bytes(kWalRecordHeaderSize);
    const auto first = reserve(writer.value(), payload.size());
    ASSERT_TRUE(writer->copy_in(first, payload));
    ASSERT_TRUE(writer->flush_through(first.end_lsn));
    const auto second = reserve(writer.value(), payload.size());
    ASSERT_TRUE(writer->copy_in(second, payload));

    auto recycle = std::async(std::launch::async, [&writer] {
        return writer->remove_segments_before(kInitialWalRingLsn);
    });
    const bool recycle_entered = faults->wait_until_entered(2s);
    EXPECT_TRUE(recycle_entered);

    auto flush = std::async(std::launch::async, [&writer, second] {
        return writer->flush_through(second.end_lsn);
    });
    EXPECT_EQ(flush.wait_for(40ms), std::future_status::timeout);
    faults->release();
    ASSERT_EQ(recycle.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(recycle.get());

    if (flush.wait_for(2s) != std::future_status::ready) {
        ADD_FAILURE() << "retention I/O completion did not wake the flush follower";
        ASSERT_TRUE(writer->write_through(second.end_lsn));
    }
    ASSERT_EQ(flush.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(flush.get());
}

TEST(WalWriterTest, RejectsInvalidTargetsAndDelays) {
    auto root = test_support::TestDirectory::create("wal-writer-invalid");
    ASSERT_TRUE(root);
    auto writer = create_writer(root->join("wal"));
    ASSERT_TRUE(writer);
    EXPECT_FALSE(writer->write_through(base::kInvalidLsn));
    EXPECT_FALSE(writer->flush_through(base::Lsn{kInitialWalRingLsn.value() + 8U}));
    EXPECT_FALSE(writer->flush_commit(kInitialWalRingLsn, -1us));
}

}  // namespace
}  // namespace db::wal
