#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/control_store.hpp"
#include "db/wal/format.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"
#include "db/wal/ring_buffer.hpp"
#include "db/wal/segment_storage.hpp"
#include "db/wal/writer.hpp"

namespace db::wal {
namespace {

inline constexpr std::uint64_t kSystemIdentifier = 0x4D4F'5645'5741'4C31ULL;
inline constexpr std::uint32_t kTimeline = 17U;

template <typename Value>
void expect_unavailable(const base::Result<Value>& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

auto storage_options(const bool create) -> WalSegmentStorageOptions {
    return WalSegmentStorageOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .retention_lsn = base::kInvalidLsn,
        .create = create,
    };
}

auto manager_options(const bool create) -> WalManagerOptions {
    return WalManagerOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .ring_page_count = 2U,
        .retention_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .create = create,
    };
}

auto initial_control() -> WalControlImage {
    return WalControlImage{
        .system_identifier = kSystemIdentifier,
        .generation = kInitialWalControlGeneration,
    };
}

auto transaction_record() -> WalRecord {
    return WalRecord{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::Xid{3U},
        .resource_manager = WalResourceManager::kTransaction,
        .info = opcode::transaction::kCommit,
        .blocks = {},
        .main_payload = {},
    };
}

TEST(WalMoveLifecycleTest, ControlStoreSourceBecomesAnInertValue) {
    auto root = test_support::TestDirectory::create("wal-control-move-lifecycle");
    ASSERT_TRUE(root);
    auto created = WalControlStore::create(root->join("wal"), initial_control());
    ASSERT_TRUE(created) << created.error().to_string();
    WalControlStore store{std::move(created).value()};
    WalControlStore live{std::move(store)};

    // Deliberately verifies the documented moved-from boundary.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(store.valid());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_TRUE(store.directory().empty());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_TRUE(store.current_path().empty());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(store.current().generation, 0U);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(store.has_staged_update());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(store.staged());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(store.publish_staged());

    EXPECT_TRUE(live.valid());
    EXPECT_EQ(live.current().generation, kInitialWalControlGeneration);
}

TEST(WalMoveLifecycleTest, SegmentStorageSourceBecomesAnInertValue) {
    auto root = test_support::TestDirectory::create("wal-segment-move-lifecycle");
    ASSERT_TRUE(root);
    auto opened = WalSegmentStorage::open(root->join("wal"), storage_options(true));
    ASSERT_TRUE(opened) << opened.error().to_string();
    WalSegmentStorage storage{std::move(opened).value()};
    WalSegmentStorage live{std::move(storage)};

    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(storage.valid());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_TRUE(storage.directory().empty());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(storage.system_identifier(), 0U);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(storage.timeline(), 0U);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(storage.logical_size(), 0U);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(storage.retention_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(storage.allocated_segment_count());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(storage.sync());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(WalReader::from_storage(storage));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(WalWriter::create(std::move(storage), 2U));

    EXPECT_TRUE(live.valid());
    EXPECT_EQ(live.system_identifier(), kSystemIdentifier);
}

TEST(WalMoveLifecycleTest, RingSourceBecomesAnInertValue) {
    auto created = WalRingBuffer::create(2U);
    ASSERT_TRUE(created) << created.error().to_string();
    WalRingBuffer ring{std::move(created).value()};
    WalRingBuffer live{std::move(ring)};

    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(ring.valid());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(ring.page_count(), 0U);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(ring.payload_capacity(), 0U);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(ring.released_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(ring.reserved_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(ring.inserted_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(ring.try_reserve(kWalRecordHeaderSize));
    std::vector<std::byte> output{std::byte{0xA5}};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(ring.copy_out(base::kInvalidLsn, base::kInvalidLsn, output));
    EXPECT_TRUE(output.empty());

    EXPECT_TRUE(live.valid());
    EXPECT_EQ(live.page_count(), 2U);
}

TEST(WalMoveLifecycleTest, WriterSourceBecomesAnInertValue) {
    auto root = test_support::TestDirectory::create("wal-writer-move-lifecycle");
    ASSERT_TRUE(root);
    auto storage = WalSegmentStorage::open(root->join("wal"), storage_options(true));
    ASSERT_TRUE(storage) << storage.error().to_string();
    auto created = WalWriter::create(std::move(storage).value(), 2U);
    ASSERT_TRUE(created) << created.error().to_string();
    WalWriter writer{std::move(created).value()};
    WalWriter live{std::move(writer)};

    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(writer.valid());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(writer.reserved_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(writer.inserted_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(writer.written_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(writer.flushed_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(writer.try_reserve(kWalRecordHeaderSize));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(writer.flush_through(base::kInvalidLsn));

    EXPECT_TRUE(live.valid());
    EXPECT_EQ(live.reserved_lsn(), kInitialWalRingLsn);
}

TEST(WalMoveLifecycleTest, ManagerSourceCannotForgeAnActiveStartupProof) {
    auto root = test_support::TestDirectory::create("wal-manager-move-lifecycle");
    ASSERT_TRUE(root);
    auto opened = WalManager::open(root->join("wal"), manager_options(true));
    ASSERT_TRUE(opened) << opened.error().to_string();
    WalManager manager{std::move(opened).value()};
    WalManager live{std::move(manager)};

    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(manager.valid());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_TRUE(manager.directory().empty());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(manager.end_lsn(), base::kInvalidLsn);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(manager.startup_validation().valid());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_TRUE(manager.is_shutdown());
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(manager.insert(transaction_record()));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_unavailable(manager.flush_through(base::kInvalidLsn));
    // A lifecycle close remains idempotent for the inert source.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_TRUE(manager.shutdown());

    EXPECT_TRUE(live.valid());
    EXPECT_TRUE(live.startup_validation().valid());
    EXPECT_TRUE(live.shutdown());
}

}  // namespace
}  // namespace db::wal
