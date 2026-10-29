#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/recovery/wal_stream.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/format.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"
#include "db/wal/segment_storage.hpp"

namespace db::recovery {
namespace {

inline constexpr std::uint64_t kSystemIdentifier = 0x6A09'E667'F3BC'C908ULL;
inline constexpr std::uint32_t kTimeline = 11U;

auto manager_options(const bool create) -> wal::WalManagerOptions {
    return wal::WalManagerOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .ring_page_count = 4U,
        .retention_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .create = create,
    };
}

auto reader_options() -> wal::WalReaderOptions {
    return wal::WalReaderOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .retention_lsn = base::kInvalidLsn,
    };
}

auto storage_options(const bool create) -> wal::WalSegmentStorageOptions {
    return wal::WalSegmentStorageOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .retention_lsn = base::kInvalidLsn,
        .create = create,
    };
}

auto transaction_record(
    const std::uint32_t xid,
    const std::uint8_t info = wal::opcode::transaction::kCommit
) -> wal::WalRecord {
    return wal::WalRecord{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::Xid{xid},
        .resource_manager = wal::WalResourceManager::kTransaction,
        .info = info,
        .blocks = {},
        .main_payload = {},
    };
}

auto stream_options(
    const std::filesystem::path& directory,
    const base::Lsn redo_lsn = base::kInvalidLsn,
    std::optional<wal::WalStartupValidation> validation = std::nullopt
) -> RecoveryWalStreamOptions {
    return RecoveryWalStreamOptions{
        .directory = directory,
        .reader_options = reader_options(),
        .redo_lsn = redo_lsn,
        .trusted_startup_validation = validation,
    };
}

void expect_inert(RecoveryWalStream& stream) {
    // Deliberately inspects the documented inert state after a move.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(stream.valid());
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(stream.replay_start_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(stream.position(), base::kInvalidLsn);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(stream.logical_end_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(stream.repaired_incomplete_tail());
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    const auto next = stream.read_next();
    ASSERT_FALSE(next);
    EXPECT_EQ(next.error().code(), base::ErrorCode::kUnavailable);
}

TEST(RecoveryWalStreamTest, PreflightsAndStreamsFromAnExactRedoBoundary) {
    auto root = test_support::TestDirectory::create("recovery-wal-stream-boundary");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    wal::WalInsertResult first{};
    wal::WalInsertResult second{};
    {
        auto manager = wal::WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        auto first_inserted = manager->insert(transaction_record(31U));
        ASSERT_TRUE(first_inserted);
        first = first_inserted.value();
        auto second_inserted = manager->insert(transaction_record(32U));
        ASSERT_TRUE(second_inserted);
        second = second_inserted.value();
        ASSERT_TRUE(manager->flush_through(second.end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    auto stream = RecoveryWalStream::open(stream_options(directory, first.end_lsn));
    ASSERT_TRUE(stream) << stream.error().to_string();
    EXPECT_EQ(stream->replay_start_lsn(), first.end_lsn);
    EXPECT_EQ(stream->position(), first.end_lsn);
    EXPECT_EQ(stream->logical_end_lsn(), second.end_lsn);
    EXPECT_FALSE(stream->repaired_incomplete_tail());

    auto decoded = stream->read_next();
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    ASSERT_TRUE(decoded->has_value());
    EXPECT_EQ(decoded->value().start_lsn, second.start_lsn);
    EXPECT_EQ(decoded->value().end_lsn, second.end_lsn);
    EXPECT_EQ(decoded->value().record.xid, base::Xid{32U});
    auto end = stream->read_next();
    ASSERT_TRUE(end) << end.error().to_string();
    EXPECT_FALSE(end->has_value());
}

TEST(RecoveryWalStreamTest, MovingTransfersTheOnlyActiveRecoveryCursor) {
    auto root = test_support::TestDirectory::create("recovery-wal-stream-moved-from");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    {
        auto manager = wal::WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager) << manager.error().to_string();
        ASSERT_TRUE(manager->shutdown());
    }

    auto opened = RecoveryWalStream::open(stream_options(directory));
    ASSERT_TRUE(opened) << opened.error().to_string();
    RecoveryWalStream stream{std::move(opened).value()};
    ASSERT_TRUE(stream.valid());
    RecoveryWalStream moved{std::move(stream)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert(stream);
    ASSERT_TRUE(moved.valid());

    auto assignment_target = RecoveryWalStream::open(stream_options(directory));
    ASSERT_TRUE(assignment_target) << assignment_target.error().to_string();
    assignment_target.value() = std::move(moved);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert(moved);
    ASSERT_TRUE(assignment_target->valid());
    auto end = assignment_target->read_next();
    ASSERT_TRUE(end) << end.error().to_string();
    EXPECT_FALSE(end->has_value());
}

TEST(RecoveryWalStreamTest, RejectsTheInactiveProofFromAMovedManagerEvenForAnEmptyStream) {
    auto root = test_support::TestDirectory::create("recovery-wal-stream-inactive-proof");
    ASSERT_TRUE(root);
    const auto empty_directory = root->join("empty-wal");
    {
        auto storage = wal::WalSegmentStorage::open(empty_directory, storage_options(true));
        ASSERT_TRUE(storage) << storage.error().to_string();
        EXPECT_EQ(storage->logical_size(), 0U);
    }

    auto opened_manager = wal::WalManager::open(root->join("managed-wal"), manager_options(true));
    ASSERT_TRUE(opened_manager) << opened_manager.error().to_string();
    wal::WalManager manager{std::move(opened_manager).value()};
    wal::WalManager live_manager{std::move(manager)};
    // Deliberately obtains the invalid sentinel from the moved-from manager.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    const auto inactive_proof = manager.startup_validation();
    ASSERT_FALSE(inactive_proof.valid());

    const auto rejected =
        RecoveryWalStream::open(stream_options(empty_directory, base::kInvalidLsn, inactive_proof));
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    ASSERT_TRUE(live_manager.shutdown());
}

TEST(RecoveryWalStreamTest, RefusesIncompleteTailUntilManagerRepairsIt) {
    auto root = test_support::TestDirectory::create("recovery-wal-stream-incomplete-tail");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    wal::WalInsertResult first{};
    wal::WalInsertResult second{};
    {
        auto manager = wal::WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        auto first_inserted = manager->insert(transaction_record(41U));
        ASSERT_TRUE(first_inserted);
        first = first_inserted.value();
        auto second_inserted = manager->insert(transaction_record(42U));
        ASSERT_TRUE(second_inserted);
        second = second_inserted.value();
        ASSERT_TRUE(manager->flush_through(second.end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    std::uint64_t incomplete_end = 0U;
    {
        auto storage = wal::WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(storage);
        ASSERT_TRUE(storage->truncate_to(second.end_lsn.value() - 8U));
        incomplete_end = storage->logical_size();
    }

    auto rejected = RecoveryWalStream::open(stream_options(directory));
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    {
        auto unchanged = wal::WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(unchanged);
        EXPECT_EQ(unchanged->logical_size(), incomplete_end);
    }

    auto manager = wal::WalManager::open(directory, manager_options(false));
    ASSERT_TRUE(manager) << manager.error().to_string();
    const auto validation = manager->startup_validation();
    EXPECT_TRUE(validation.repaired_incomplete_tail());
    EXPECT_EQ(validation.stream_start_lsn(), base::Lsn{wal::kWalPageSize});
    EXPECT_EQ(validation.end_lsn(), first.end_lsn);

    auto repaired_stream =
        RecoveryWalStream::open(stream_options(directory, base::kInvalidLsn, validation));
    ASSERT_TRUE(repaired_stream) << repaired_stream.error().to_string();
    EXPECT_TRUE(repaired_stream->repaired_incomplete_tail());
    auto decoded = repaired_stream->read_next();
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    ASSERT_TRUE(decoded->has_value());
    EXPECT_EQ(decoded->value().record.xid, base::Xid{41U});
    auto end = repaired_stream->read_next();
    ASSERT_TRUE(end) << end.error().to_string();
    EXPECT_FALSE(end->has_value());
    ASSERT_TRUE(manager->shutdown());
}

TEST(RecoveryWalStreamTest, RejectsAStaleTrustedTailWithoutMutatingWal) {
    auto root = test_support::TestDirectory::create("recovery-wal-stream-stale-tail");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    {
        auto manager = wal::WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        auto inserted = manager->insert(transaction_record(51U));
        ASSERT_TRUE(inserted);
        ASSERT_TRUE(manager->flush_through(inserted->end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    auto validated_manager = wal::WalManager::open(directory, manager_options(false));
    ASSERT_TRUE(validated_manager);
    const auto validation = validated_manager->startup_validation();
    EXPECT_EQ(validation.stream_start_lsn(), base::Lsn{wal::kWalPageSize});
    EXPECT_FALSE(validation.repaired_incomplete_tail());
    ASSERT_TRUE(validated_manager->shutdown());

    std::uint64_t changed_end = 0U;
    {
        auto storage = wal::WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(storage);
        const std::array partial{std::byte{0xD1}, std::byte{0x5E}, std::byte{0xA5}};
        ASSERT_TRUE(storage->write_all_at(storage->logical_size(), partial));
        ASSERT_TRUE(storage->sync());
        changed_end = storage->logical_size();
    }

    auto rejected =
        RecoveryWalStream::open(stream_options(directory, base::kInvalidLsn, validation));
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    auto unchanged = wal::WalSegmentStorage::open(directory, storage_options(false));
    ASSERT_TRUE(unchanged);
    EXPECT_EQ(unchanged->logical_size(), changed_end);
}

TEST(RecoveryWalStreamTest, RejectsUnknownRecordWithoutTruncatingIt) {
    auto root = test_support::TestDirectory::create("recovery-wal-stream-unknown-record");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    {
        auto manager = wal::WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        auto inserted = manager->insert(transaction_record(61U, 0x7FU));
        ASSERT_TRUE(inserted);
        ASSERT_TRUE(manager->flush_through(inserted->end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    std::uint64_t original_end = 0U;
    {
        auto storage = wal::WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(storage);
        original_end = storage->logical_size();
    }

    auto rejected = RecoveryWalStream::open(stream_options(directory));
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    auto unchanged = wal::WalSegmentStorage::open(directory, storage_options(false));
    ASSERT_TRUE(unchanged);
    EXPECT_EQ(unchanged->logical_size(), original_end);
    EXPECT_FALSE(wal::WalManager::open(directory, manager_options(false)));
}

TEST(RecoveryWalStreamTest, RejectsInvalidRedoPointersAndMakesReadErrorsTerminal) {
    auto root = test_support::TestDirectory::create("recovery-wal-stream-invalid-redo");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    wal::WalInsertResult inserted{};
    {
        auto manager = wal::WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        auto record = transaction_record(71U);
        record.main_payload = {std::byte{0x71}};
        auto result = manager->insert(record);
        ASSERT_TRUE(result);
        inserted = result.value();
        ASSERT_TRUE(manager->flush_through(inserted.end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    auto inside_record = RecoveryWalStream::open(
        stream_options(directory, base::Lsn{inserted.start_lsn.value() + 8U})
    );
    ASSERT_FALSE(inside_record);
    EXPECT_EQ(inside_record.error().code(), base::ErrorCode::kCorruption);
    auto outside_stream = RecoveryWalStream::open(
        stream_options(directory, base::Lsn{inserted.end_lsn.value() + 8U})
    );
    ASSERT_FALSE(outside_stream);
    EXPECT_EQ(outside_stream.error().code(), base::ErrorCode::kCorruption);

    auto validated_manager = wal::WalManager::open(directory, manager_options(false));
    ASSERT_TRUE(validated_manager);
    const auto validation = validated_manager->startup_validation();
    ASSERT_TRUE(validated_manager->shutdown());
    {
        auto storage = wal::WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(storage);
        std::array<std::byte, 1> byte{};
        const auto payload_lsn = inserted.start_lsn.value() + wal::kWalRecordHeaderSize;
        ASSERT_TRUE(storage->read_exact_at(payload_lsn, byte));
        byte.front() ^= std::byte{1};
        ASSERT_TRUE(storage->write_all_at(payload_lsn, byte));
        ASSERT_TRUE(storage->sync());
    }

    auto stream = RecoveryWalStream::open(stream_options(directory, base::kInvalidLsn, validation));
    ASSERT_TRUE(stream) << stream.error().to_string();
    auto first_failure = stream->read_next();
    ASSERT_FALSE(first_failure);
    EXPECT_EQ(first_failure.error().code(), base::ErrorCode::kCorruption);
    auto repeated_failure = stream->read_next();
    ASSERT_FALSE(repeated_failure);
    EXPECT_EQ(repeated_failure.error().code(), base::ErrorCode::kCorruption);
    auto unchanged = wal::WalSegmentStorage::open(directory, storage_options(false));
    ASSERT_TRUE(unchanged);
    EXPECT_EQ(unchanged->logical_size(), validation.end_lsn().value());
}

}  // namespace
}  // namespace db::recovery
