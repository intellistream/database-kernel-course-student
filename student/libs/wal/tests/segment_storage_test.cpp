#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/io/posix.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/format.hpp"
#include "db/wal/segment_storage.hpp"

#include "wal_fault_port.hpp"

namespace db::wal {
namespace {

inline constexpr std::uint64_t kSystemIdentifier = 0x1020'3040'5060'7080ULL;
inline constexpr std::uint32_t kTimeline = 3;

auto options(
    const bool create,
    const base::Lsn retention_lsn = base::kInvalidLsn,
    std::shared_ptr<WalStorageFaultPort> fault_port = nullptr
) -> WalSegmentStorageOptions {
    return WalSegmentStorageOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .retention_lsn = retention_lsn,
        .create = create,
        .fault_port = std::move(fault_port),
    };
}

auto patterned_bytes(const std::size_t size) -> std::vector<std::byte> {
    std::vector<std::byte> bytes(size);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>((index * 31U + 7U) & 0xFFU);
    }
    return bytes;
}

TEST(WalSegmentStorageTest, PreallocatesCanonicalDurableHeaderOnlySegments) {
    auto root = test_support::TestDirectory::create("wal-segment-preallocate");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    {
        auto storage = WalSegmentStorage::open(directory, options(true));
        ASSERT_TRUE(storage);
        EXPECT_EQ(storage->directory(), directory);
        EXPECT_EQ(storage->system_identifier(), kSystemIdentifier);
        EXPECT_EQ(storage->timeline(), kTimeline);
        EXPECT_EQ(storage->logical_size(), 0U);
        ASSERT_TRUE(storage->preallocate_through(2U * kWalSegmentSize + 1U));
        EXPECT_EQ(storage->allocated_segment_count().value(), 3U);
        EXPECT_EQ(storage->segment_path(2).filename().string(), "0000000000000002.wal");

        auto file = io::PosixFile::open(storage->segment_path(2));
        ASSERT_TRUE(file);
        EXPECT_EQ(file->size().value(), kWalSegmentHeaderSize);
        WalSegmentHeaderBytes encoded{};
        ASSERT_TRUE(file->read_exact_at(0, encoded));
        EXPECT_TRUE(base::verify_crc32c(encoded, kWalSegmentChecksumOffset));
        const auto header = decode_segment_header(encoded);
        ASSERT_TRUE(header);
        EXPECT_EQ(header->system_identifier, kSystemIdentifier);
        EXPECT_EQ(header->timeline, kTimeline);
        EXPECT_EQ(header->segment_number, 2U);
        EXPECT_EQ(header->first_lsn, base::Lsn{2U * kWalSegmentSize});
    }

    const auto reopened = WalSegmentStorage::open(directory, options(false));
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->logical_size(), 0U);
    EXPECT_EQ(reopened->allocated_segment_count().value(), 3U);
}

TEST(WalSegmentStorageTest, LogicalIoCrossesPhysicalHeadersAndReopens) {
    auto root = test_support::TestDirectory::create("wal-segment-boundary");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    const auto payload = patterned_bytes(static_cast<std::size_t>(kWalSegmentSize) + 257U);
    {
        auto storage = WalSegmentStorage::open(directory, options(true));
        ASSERT_TRUE(storage);
        ASSERT_TRUE(storage->write_all_at(0, payload));
        ASSERT_TRUE(storage->sync());
        EXPECT_EQ(storage->logical_size(), payload.size());
        EXPECT_EQ(storage->allocated_segment_count().value(), 2U);

        std::array<std::byte, 512> boundary{};
        ASSERT_TRUE(storage->read_exact_at(kWalSegmentSize - 255U, boundary));
        EXPECT_TRUE(
            std::equal(
                boundary.begin(),
                boundary.end(),
                payload.begin() + static_cast<std::ptrdiff_t>(kWalSegmentSize - 255U)
            )
        );
    }

    const auto reopened = WalSegmentStorage::open(directory, options(false));
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->logical_size(), payload.size());
    std::array<std::byte, 257> tail{};
    ASSERT_TRUE(reopened->read_exact_at(kWalSegmentSize, tail));
    EXPECT_TRUE(
        std::equal(
            tail.begin(),
            tail.end(),
            payload.end() - static_cast<std::ptrdiff_t>(tail.size())
        )
    );
}

TEST(WalSegmentStorageTest, RejectsForeignAndChecksumCorruptSegmentHeaders) {
    auto root = test_support::TestDirectory::create("wal-segment-foreign");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    std::filesystem::path segment;
    {
        auto storage = WalSegmentStorage::open(directory, options(true));
        ASSERT_TRUE(storage);
        ASSERT_TRUE(storage->preallocate_through(1));
        segment = storage->segment_path(0);
    }

    auto file =
        io::PosixFile::open(segment, io::FileOpenOptions{.access = io::FileAccess::kReadWrite});
    ASSERT_TRUE(file);
    WalSegmentHeaderBytes encoded{};
    ASSERT_TRUE(file->read_exact_at(0, encoded));
    base::LittleEndianWriter identity_writer{
        base::MutableBytes{encoded}.subspan(24, sizeof(std::uint64_t))
    };
    ASSERT_TRUE(identity_writer.write_u64(kSystemIdentifier + 1U));
    ASSERT_TRUE(base::seal_crc32c(encoded, kWalSegmentChecksumOffset));
    ASSERT_TRUE(file->write_all_at(0, encoded));
    ASSERT_TRUE(file->sync_data());
    ASSERT_TRUE(file->close());

    const auto foreign = WalSegmentStorage::open(directory, options(false));
    ASSERT_FALSE(foreign);
    EXPECT_EQ(foreign.error().code(), base::ErrorCode::kCorruption);

    encoded.front() ^= std::byte{1};
    auto corrupt_file =
        io::PosixFile::open(segment, io::FileOpenOptions{.access = io::FileAccess::kReadWrite});
    ASSERT_TRUE(corrupt_file);
    ASSERT_TRUE(corrupt_file->write_all_at(0, encoded));
    ASSERT_TRUE(corrupt_file->sync_data());
    EXPECT_FALSE(WalSegmentStorage::open(directory, options(false)));
}

TEST(WalSegmentStorageTest, RejectsMissingRequiredSegmentsAndDataAfterPartialTail) {
    auto root = test_support::TestDirectory::create("wal-segment-gap");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    {
        auto storage = WalSegmentStorage::open(directory, options(true));
        ASSERT_TRUE(storage);
        ASSERT_TRUE(storage->preallocate_through(2U * kWalSegmentSize + 1U));
    }
    ASSERT_TRUE(io::remove_file(directory / "0000000000000001.wal", false));
    EXPECT_FALSE(WalSegmentStorage::open(directory, options(false)));

    auto second_root = test_support::TestDirectory::create("wal-segment-partial-chain");
    ASSERT_TRUE(second_root);
    const auto second_directory = second_root->join("wal");
    {
        auto storage = WalSegmentStorage::open(second_directory, options(true));
        ASSERT_TRUE(storage);
        ASSERT_TRUE(storage->preallocate_through(kWalSegmentSize + 1U));
    }
    auto second = io::PosixFile::open(
        second_directory / "0000000000000001.wal",
        io::FileOpenOptions{.access = io::FileAccess::kReadWrite}
    );
    ASSERT_TRUE(second);
    const std::array<std::byte, 1> marker{std::byte{0xA5}};
    ASSERT_TRUE(second->write_all_at(kWalSegmentHeaderSize, marker));
    ASSERT_TRUE(second->sync_data());
    EXPECT_FALSE(WalSegmentStorage::open(second_directory, options(false)));
}

TEST(WalSegmentStorageTest, TruncatesOnlyAtValidatedLogicalBoundaries) {
    auto root = test_support::TestDirectory::create("wal-segment-truncate");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    const auto payload = patterned_bytes(static_cast<std::size_t>(kWalSegmentSize) + 128U);
    const auto truncated_size = kWalSegmentSize - 10U;
    {
        auto storage = WalSegmentStorage::open(directory, options(true));
        ASSERT_TRUE(storage);
        ASSERT_TRUE(storage->write_all_at(0, payload));
        ASSERT_TRUE(storage->sync());
        ASSERT_TRUE(storage->preallocate_through(2U * kWalSegmentSize + 1U));
        EXPECT_EQ(storage->allocated_segment_count().value(), 3U);
        ASSERT_TRUE(storage->truncate_to(truncated_size));
        EXPECT_EQ(storage->logical_size(), truncated_size);
        EXPECT_EQ(storage->allocated_segment_count().value(), 1U);
        std::array<std::byte, 1> byte{};
        EXPECT_FALSE(storage->read_exact_at(truncated_size, byte));
        EXPECT_FALSE(storage->truncate_to(truncated_size + 1U));
    }
    auto reopened = WalSegmentStorage::open(directory, options(false));
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->logical_size(), truncated_size);
    EXPECT_EQ(
        std::filesystem::file_size(reopened->segment_path(0)),
        kWalSegmentHeaderSize + truncated_size
    );
    ASSERT_TRUE(reopened->truncate_to(0));
    EXPECT_EQ(reopened->logical_size(), 0U);
    EXPECT_EQ(std::filesystem::file_size(reopened->segment_path(0)), kWalSegmentHeaderSize);
}

TEST(WalSegmentStorageTest, RetentionFirstCleanupSurvivesBothCrashBoundaries) {
    auto root = test_support::TestDirectory::create("wal-segment-retention");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    const auto payload = patterned_bytes(static_cast<std::size_t>(kWalSegmentSize) + 128U);
    const base::Lsn horizon{kWalSegmentSize + 64U};
    {
        auto storage = WalSegmentStorage::open(directory, options(true));
        ASSERT_TRUE(storage);
        ASSERT_TRUE(storage->write_all_at(0, payload));
        ASSERT_TRUE(storage->sync());
    }

    // The external control horizon is durable but old files are still present.
    std::array<std::byte, 8> marker{};
    {
        auto retained = WalSegmentStorage::open(directory, options(false, horizon));
        ASSERT_TRUE(retained);
        EXPECT_EQ(retained->retention_lsn(), horizon);
        EXPECT_EQ(retained->allocated_segment_count().value(), 2U);
        ASSERT_TRUE(retained->read_exact_at(horizon.value(), marker));
        EXPECT_TRUE(
            std::equal(
                marker.begin(),
                marker.end(),
                payload.begin() + static_cast<std::ptrdiff_t>(horizon.value())
            )
        );
        std::array<std::byte, 1> old_byte{};
        EXPECT_FALSE(retained->read_exact_at(horizon.value() - 1U, old_byte));
        ASSERT_TRUE(retained->remove_segments_before(horizon));
        EXPECT_EQ(retained->allocated_segment_count().value(), 1U);
        EXPECT_FALSE(retained->remove_segments_before(base::Lsn{horizon.value() - 1U}));
        EXPECT_FALSE(retained->remove_segments_before(
            base::Lsn{
                payload.size() + 1U,
            }
        ));
    }

    const auto reopened = WalSegmentStorage::open(directory, options(false, horizon));
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->logical_size(), payload.size());
    EXPECT_EQ(reopened->allocated_segment_count().value(), 1U);
    EXPECT_TRUE(reopened->read_exact_at(horizon.value(), marker));
}

TEST(WalSegmentStorageTest, RejectsGapsOverflowAndExcessivePreallocation) {
    auto root = test_support::TestDirectory::create("wal-segment-errors");
    ASSERT_TRUE(root);
    auto storage = WalSegmentStorage::open(root->join("wal"), options(true));
    ASSERT_TRUE(storage);
    const std::array<std::byte, 2> bytes{std::byte{1}, std::byte{2}};
    EXPECT_FALSE(storage->write_all_at(1, base::Bytes{bytes}.first(1)));
    EXPECT_FALSE(storage->write_all_at(std::numeric_limits<std::uint64_t>::max(), bytes));
    std::array<std::byte, 1> output{};
    EXPECT_FALSE(storage->read_exact_at(0, output));
    EXPECT_FALSE(storage->truncate_to(1));
    EXPECT_FALSE(storage->remove_segments_before(base::Lsn{1}));
    EXPECT_FALSE(
        storage->preallocate_through((kMaximumWalPreallocationSegments + 1U) * kWalSegmentSize)
    );
    EXPECT_FALSE(WalSegmentStorage::open({}, options(true)));
    auto bad = options(true);
    bad.system_identifier = 0;
    EXPECT_FALSE(WalSegmentStorage::open(root->join("bad"), bad));
}

TEST(WalSegmentStorageTest, ConvertsFaultPortExceptionsWithoutMutatingTheStream) {
    auto root = test_support::TestDirectory::create("wal-segment-fault-port-exceptions");
    ASSERT_TRUE(root);
    auto faults = std::make_shared<test::OneShotThrowingWalStorageFaultPort>();
    auto storage =
        WalSegmentStorage::open(root->join("wal"), options(true, base::kInvalidLsn, faults));
    ASSERT_TRUE(storage);
    const std::array payload{std::byte{0x5A}};

    for (const auto mode : {
             test::WalStorageFaultException::kAllocation,
             test::WalStorageFaultException::kLength,
         }) {
        faults->arm(WalStorageFaultPoint::kWrite, mode);
        auto failed = storage->write_all_at(0U, payload);
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error().code(), base::ErrorCode::kResourceExhausted);
        EXPECT_EQ(storage->logical_size(), 0U);
    }

    faults->arm(WalStorageFaultPoint::kWrite, test::WalStorageFaultException::kStandard);
    auto standard = storage->write_all_at(0U, payload);
    ASSERT_FALSE(standard);
    EXPECT_EQ(standard.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(
        standard.error().message().find("injected WAL storage exception"),
        std::string_view::npos
    );
    EXPECT_EQ(storage->logical_size(), 0U);

    faults->arm(WalStorageFaultPoint::kWrite, test::WalStorageFaultException::kUnknown);
    auto unknown = storage->write_all_at(0U, payload);
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code(), base::ErrorCode::kInternal);
    EXPECT_EQ(storage->logical_size(), 0U);

    ASSERT_TRUE(storage->write_all_at(0U, payload));
    ASSERT_TRUE(storage->sync());
    EXPECT_EQ(storage->logical_size(), payload.size());
}

}  // namespace
}  // namespace db::wal
