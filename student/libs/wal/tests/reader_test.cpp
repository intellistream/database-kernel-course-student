#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/delta.hpp"
#include "db/wal/format.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"
#include "db/wal/segment_storage.hpp"

namespace db::wal {
namespace {

inline constexpr std::uint64_t kSystemIdentifier = 0x3141'5926'5358'9793ULL;
inline constexpr std::uint32_t kTimeline = 7;

auto manager_options(const bool create) -> WalManagerOptions {
    return WalManagerOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .ring_page_count = 4,
        .retention_lsn = base::kInvalidLsn,
        .create = create,
    };
}

auto reader_options() -> WalReaderOptions {
    return WalReaderOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .retention_lsn = base::kInvalidLsn,
    };
}

auto storage_options(const bool create) -> WalSegmentStorageOptions {
    return WalSegmentStorageOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = kTimeline,
        .retention_lsn = base::kInvalidLsn,
        .create = create,
    };
}

auto transaction_record(
    const std::uint32_t xid,
    const std::size_t payload_size = 8,
    const std::uint8_t info = opcode::transaction::kCommit
) -> WalRecord {
    return WalRecord{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::Xid{xid},
        .resource_manager = WalResourceManager::kTransaction,
        .info = info,
        .blocks = {},
        .main_payload = std::vector<std::byte>(payload_size, static_cast<std::byte>(xid & 0xFFU)),
    };
}

auto heap_record(const std::uint8_t info, const bool with_block) -> WalRecord {
    std::vector<WalBlockReference> blocks;
    if (with_block) {
        blocks.push_back(
            WalBlockReference{
                .relation_id = base::RelationId{9},
                .page_id = base::PageId{4},
                .will_initialize = false,
                .standard_page = false,
                .full_page_image = std::nullopt,
                .delta = encode_page_delta(
                             std::array{
                                 PageDeltaPatch{.offset = 8, .bytes = {std::byte{0xA5}}},
                             }
                )
                             .value(),
            }
        );
    }
    return WalRecord{
        .previous_lsn = base::kInvalidLsn,
        .xid = base::Xid{11},
        .resource_manager = WalResourceManager::kHeap,
        .info = info,
        .blocks = std::move(blocks),
        .main_payload = {},
    };
}

void expect_inert_observers(WalReader& reader) {
    // Deliberately inspects the documented inert state after a move.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(reader.valid());
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(reader.position(), base::kInvalidLsn);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(reader.stream_start(), base::kInvalidLsn);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(reader.logical_end(), base::kInvalidLsn);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_EQ(reader.storage_read_count(), 0U);
}

void expect_inert_operations(WalReader& reader) {
    // Deliberately invokes each structured operation after a move.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    const auto seek = reader.seek(base::kInvalidLsn);
    ASSERT_FALSE(seek);
    EXPECT_EQ(seek.error().code(), base::ErrorCode::kUnavailable);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    const auto next = reader.read_next();
    ASSERT_FALSE(next);
    EXPECT_EQ(next.error().code(), base::ErrorCode::kUnavailable);
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    const auto scan = reader.scan_valid_end(base::kInvalidLsn);
    ASSERT_FALSE(scan);
    EXPECT_EQ(scan.error().code(), base::ErrorCode::kUnavailable);
}

TEST(WalReaderTest, ReadsForwardSeeksAndSharesReadAhead) {
    auto root = test_support::TestDirectory::create("wal-reader-forward");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    std::vector<WalInsertResult> inserted;
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        for (std::uint32_t xid = 1; xid <= 3; ++xid) {
            auto result = manager->insert(transaction_record(xid, 1));
            ASSERT_TRUE(result);
            inserted.push_back(result.value());
        }
        ASSERT_TRUE(manager->flush_through(inserted.back().end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    auto reader = WalReader::open(directory, reader_options());
    ASSERT_TRUE(reader);
    EXPECT_EQ(reader->stream_start(), base::Lsn{kWalPageSize});
    EXPECT_EQ(reader->position(), base::Lsn{kWalPageSize});
    EXPECT_FALSE(reader->seek(base::Lsn{1}));
    EXPECT_FALSE(reader->seek(base::Lsn{inserted.front().start_lsn.value() + 1U}));

    for (std::size_t index = 0; index < inserted.size(); ++index) {
        auto decoded = reader->read_next();
        ASSERT_TRUE(decoded);
        ASSERT_TRUE(decoded->has_value());
        EXPECT_EQ(decoded->value().start_lsn, inserted.at(index).start_lsn);
        EXPECT_EQ(decoded->value().end_lsn, inserted.at(index).end_lsn);
        EXPECT_EQ(decoded->value().record.xid, base::Xid{static_cast<std::uint32_t>(index + 1U)});
    }
    EXPECT_FALSE(reader->read_next()->has_value());
    EXPECT_EQ(reader->storage_read_count(), 1U);

    ASSERT_TRUE(reader->seek(inserted.front().start_lsn));
    const auto first_again = reader->read_next();
    ASSERT_TRUE(first_again);
    ASSERT_TRUE(first_again->has_value());
    EXPECT_EQ(first_again->value().record.xid, base::Xid{1});
    EXPECT_EQ(reader->storage_read_count(), 1U);
}

TEST(WalReaderTest, MovedFromReaderIsAnInertStructuredErrorBoundary) {
    auto root = test_support::TestDirectory::create("wal-reader-moved-from");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager) << manager.error().to_string();
        ASSERT_TRUE(manager->shutdown());
    }

    auto opened = WalReader::open(directory, reader_options());
    ASSERT_TRUE(opened) << opened.error().to_string();
    WalReader reader{std::move(opened).value()};
    ASSERT_TRUE(reader.valid());
    WalReader moved{std::move(reader)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert_observers(reader);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert_operations(reader);
    ASSERT_TRUE(moved.valid());

    auto assignment_target = WalReader::open(directory, reader_options());
    ASSERT_TRUE(assignment_target) << assignment_target.error().to_string();
    assignment_target.value() = std::move(moved);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert_observers(moved);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert_operations(moved);
    ASSERT_TRUE(assignment_target->valid());
    auto end = assignment_target->read_next();
    ASSERT_TRUE(end) << end.error().to_string();
    EXPECT_FALSE(end->has_value());
}

TEST(WalReaderTest, StreamsOneRecordAcrossPagesAndSegments) {
    auto root = test_support::TestDirectory::create("wal-reader-segment-stream");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    const auto expected = transaction_record(17, static_cast<std::size_t>(kWalSegmentSize));
    WalInsertResult inserted{};
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        const auto result = manager->insert(expected);
        ASSERT_TRUE(result);
        inserted = result.value();
        ASSERT_LT(inserted.start_lsn.value(), kWalSegmentSize);
        ASSERT_GT(inserted.end_lsn.value(), kWalSegmentSize);
        ASSERT_TRUE(manager->flush_through(inserted.end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    auto reader = WalReader::open(directory, reader_options());
    ASSERT_TRUE(reader);
    auto decoded = reader->read_next();
    ASSERT_TRUE(decoded);
    ASSERT_TRUE(decoded->has_value());
    EXPECT_EQ(decoded->value().start_lsn, inserted.start_lsn);
    EXPECT_EQ(decoded->value().end_lsn, inserted.end_lsn);
    EXPECT_EQ(decoded->value().record, expected);
    EXPECT_FALSE(reader->read_next()->has_value());
    EXPECT_GT(reader->storage_read_count(), 1U);
}

TEST(WalReaderTest, SkipsOnlyCanonicalZeroPageTailPadding) {
    auto root = test_support::TestDirectory::create("wal-reader-page-padding");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    WalInsertResult first{};
    WalInsertResult second{};
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        first = manager->insert(transaction_record(1, 8088)).value();
        second = manager->insert(transaction_record(2, 0)).value();
        ASSERT_EQ(kWalPageSize - first.end_lsn.value() % kWalPageSize, 32U);
        ASSERT_EQ(second.start_lsn.value(), first.end_lsn.value() + 32U + kWalPageHeaderSize);
        ASSERT_TRUE(manager->flush_through(second.end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    auto reader = WalReader::open(directory, reader_options());
    ASSERT_TRUE(reader);
    ASSERT_TRUE(reader->read_next()->has_value());
    const auto decoded_second = reader->read_next();
    ASSERT_TRUE(decoded_second);
    ASSERT_TRUE(decoded_second->has_value());
    EXPECT_EQ(decoded_second->value().start_lsn, second.start_lsn);
    EXPECT_FALSE(reader->read_next()->has_value());

    {
        auto storage = WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(storage);
        const std::array<std::byte, 1> non_zero{std::byte{1}};
        ASSERT_TRUE(storage->write_all_at(first.end_lsn.value(), non_zero));
        ASSERT_TRUE(storage->sync());
    }
    auto corrupt_reader = WalReader::open(directory, reader_options());
    ASSERT_TRUE(corrupt_reader);
    ASSERT_TRUE(corrupt_reader->read_next()->has_value());
    const auto corrupt_padding = corrupt_reader->read_next();
    ASSERT_FALSE(corrupt_padding);
    EXPECT_EQ(corrupt_padding.error().code(), base::ErrorCode::kCorruption);
}

TEST(WalReaderTest, RejectsAContinuationPageMarkedAsARecordStart) {
    auto root = test_support::TestDirectory::create("wal-reader-continuation");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    WalInsertResult inserted{};
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        const auto result = manager->insert(transaction_record(1, kWalPageSize));
        ASSERT_TRUE(result);
        inserted = result.value();
        ASSERT_TRUE(manager->flush_through(inserted.end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    const auto continuation_page = (inserted.start_lsn.value() / kWalPageSize + 1U) * kWalPageSize;
    {
        auto storage = WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(storage);
        WalPageHeaderBytes encoded{};
        ASSERT_TRUE(storage->read_exact_at(continuation_page, encoded));
        auto header = decode_page_header(encoded);
        ASSERT_TRUE(header);
        ASSERT_EQ(header->flags, kWalPageContinuation);
        header->flags = 0;
        header->checksum = 0;
        auto rewritten = encode_page_header(header.value());
        ASSERT_TRUE(rewritten);
        ASSERT_TRUE(base::seal_crc32c(rewritten.value(), kWalPageChecksumOffset));
        ASSERT_TRUE(storage->write_all_at(continuation_page, rewritten.value()));
        ASSERT_TRUE(storage->sync());
    }

    auto reader = WalReader::open(directory, reader_options());
    ASSERT_TRUE(reader);
    const auto decoded = reader->read_next();
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code(), base::ErrorCode::kCorruption);
    EXPECT_FALSE(WalManager::open(directory, manager_options(false)));
}

TEST(WalReaderTest, RepairsOnlyAPhysicallyIncompleteFinalRecord) {
    auto root = test_support::TestDirectory::create("wal-reader-tail-repair");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    WalInsertResult first{};
    WalInsertResult second{};
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        first = manager->insert(transaction_record(1)).value();
        second = manager->insert(transaction_record(2, 128)).value();
        ASSERT_TRUE(manager->flush_through(second.end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }
    {
        auto storage = WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(storage);
        ASSERT_TRUE(storage->truncate_to(second.end_lsn.value() - 8U));
        auto reader = WalReader::from_storage(storage.value());
        ASSERT_TRUE(reader);
        const auto scan = reader->scan_valid_end(base::kInvalidLsn);
        ASSERT_TRUE(scan);
        EXPECT_EQ(scan->valid_end_lsn, first.end_lsn);
        EXPECT_EQ(scan->records_scanned, 1U);
        EXPECT_TRUE(scan->incomplete_tail);
    }

    auto manager = WalManager::open(directory, manager_options(false));
    ASSERT_TRUE(manager);
    EXPECT_EQ(manager->end_lsn(), first.end_lsn);
    const auto replacement = manager->insert(transaction_record(3));
    ASSERT_TRUE(replacement);
    EXPECT_EQ(replacement->start_lsn, first.end_lsn);
    ASSERT_TRUE(manager->flush_through(replacement->end_lsn));
}

TEST(WalReaderTest, RejectsCompleteRecordCrcCorruptionWithoutTruncating) {
    auto root = test_support::TestDirectory::create("wal-reader-hard-corruption");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    WalInsertResult inserted{};
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        inserted = manager->insert(transaction_record(9, 32)).value();
        ASSERT_TRUE(manager->flush_through(inserted.end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }

    std::uint64_t original_size = 0;
    {
        auto storage = WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(storage);
        original_size = storage->logical_size();
        std::array<std::byte, 1> byte{};
        const auto payload_lsn = inserted.start_lsn.value() + kWalRecordHeaderSize;
        ASSERT_TRUE(storage->read_exact_at(payload_lsn, byte));
        byte.front() ^= std::byte{1};
        ASSERT_TRUE(storage->write_all_at(payload_lsn, byte));
        ASSERT_TRUE(storage->sync());
    }

    auto reader = WalReader::open(directory, reader_options());
    ASSERT_TRUE(reader);
    const auto scan = reader->scan_valid_end(base::kInvalidLsn);
    ASSERT_FALSE(scan);
    EXPECT_EQ(scan.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(reader->position(), inserted.start_lsn);
    EXPECT_FALSE(WalManager::open(directory, manager_options(false)));
    const auto unchanged = WalSegmentStorage::open(directory, storage_options(false));
    ASSERT_TRUE(unchanged);
    EXPECT_EQ(unchanged->logical_size(), original_size);
}

TEST(WalReaderTest, RejectsACompleteRecordWithAnImpossibleLength) {
    auto root = test_support::TestDirectory::create("wal-reader-invalid-length");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    WalInsertResult inserted{};
    {
        auto manager = WalManager::open(directory, manager_options(true));
        ASSERT_TRUE(manager);
        inserted = manager->insert(transaction_record(5, 32)).value();
        ASSERT_TRUE(manager->flush_through(inserted.end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }
    {
        auto storage = WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(storage);
        std::array<std::byte, sizeof(std::uint32_t)> length{};
        base::LittleEndianWriter writer{length};
        ASSERT_TRUE(writer.write_u32(kWalRecordHeaderSize));
        ASSERT_TRUE(storage->write_all_at(inserted.start_lsn.value() + 8U, length));
        ASSERT_TRUE(storage->sync());
    }

    auto reader = WalReader::open(directory, reader_options());
    ASSERT_TRUE(reader);
    const auto scan = reader->scan_valid_end(base::kInvalidLsn);
    ASSERT_FALSE(scan);
    EXPECT_EQ(scan.error().code(), base::ErrorCode::kCorruption);
}

TEST(WalReaderTest, RejectsUnknownOpcodesAndWrongBlockRoles) {
    auto unknown_root = test_support::TestDirectory::create("wal-reader-unknown-opcode");
    ASSERT_TRUE(unknown_root);
    const auto unknown_directory = unknown_root->join("wal");
    {
        auto manager = WalManager::open(unknown_directory, manager_options(true));
        ASSERT_TRUE(manager);
        const auto inserted = manager->insert(transaction_record(1, 0, 0x7F));
        ASSERT_TRUE(inserted);
        ASSERT_TRUE(manager->flush_through(inserted->end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }
    auto unknown_reader = WalReader::open(unknown_directory, reader_options());
    ASSERT_TRUE(unknown_reader);
    const auto unknown = unknown_reader->read_next();
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code(), base::ErrorCode::kCorruption);

    auto role_root = test_support::TestDirectory::create("wal-reader-block-role");
    ASSERT_TRUE(role_root);
    const auto role_directory = role_root->join("wal");
    {
        auto manager = WalManager::open(role_directory, manager_options(true));
        ASSERT_TRUE(manager);
        const auto inserted = manager->insert(heap_record(opcode::heap::kInsert, false));
        ASSERT_TRUE(inserted);
        ASSERT_TRUE(manager->flush_through(inserted->end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }
    auto role_reader = WalReader::open(role_directory, reader_options());
    ASSERT_TRUE(role_reader);
    const auto wrong_role = role_reader->read_next();
    ASSERT_FALSE(wrong_role);
    EXPECT_EQ(wrong_role.error().code(), base::ErrorCode::kCorruption);

    auto duplicate_root = test_support::TestDirectory::create("wal-reader-duplicate-block");
    ASSERT_TRUE(duplicate_root);
    const auto duplicate_directory = duplicate_root->join("wal");
    {
        auto manager = WalManager::open(duplicate_directory, manager_options(true));
        ASSERT_TRUE(manager);
        auto record = heap_record(opcode::heap::kUpdate, true);
        record.blocks.push_back(record.blocks.front());
        const auto inserted = manager->insert(record);
        ASSERT_TRUE(inserted);
        ASSERT_TRUE(manager->flush_through(inserted->end_lsn));
        ASSERT_TRUE(manager->shutdown());
    }
    auto duplicate_reader = WalReader::open(duplicate_directory, reader_options());
    ASSERT_TRUE(duplicate_reader);
    const auto duplicate = duplicate_reader->read_next();
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kCorruption);
}

TEST(WalReaderTest, RepairsAZeroPartialGuardButRejectsNonZeroGuardBytes) {
    auto root = test_support::TestDirectory::create("wal-reader-partial-guard");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    {
        auto storage = WalSegmentStorage::open(directory, storage_options(true));
        ASSERT_TRUE(storage);
        const std::array<std::byte, 100> partial{};
        ASSERT_TRUE(storage->write_all_at(0, partial));
        ASSERT_TRUE(storage->sync());
        auto reader = WalReader::from_storage(storage.value());
        ASSERT_TRUE(reader);
        const auto scan = reader->scan_valid_end(base::kInvalidLsn);
        ASSERT_TRUE(scan);
        EXPECT_EQ(scan->valid_end_lsn, base::kInvalidLsn);
        EXPECT_TRUE(scan->incomplete_tail);
    }
    auto repaired = WalManager::open(directory, manager_options(false));
    ASSERT_TRUE(repaired);
    EXPECT_EQ(repaired->end_lsn(), base::Lsn{kWalPageSize});
    ASSERT_TRUE(repaired->shutdown());
    {
        auto storage = WalSegmentStorage::open(directory, storage_options(false));
        ASSERT_TRUE(storage);
        EXPECT_EQ(storage->logical_size(), kWalPageSize);
    }

    auto corrupt_root = test_support::TestDirectory::create("wal-reader-corrupt-guard");
    ASSERT_TRUE(corrupt_root);
    const auto corrupt_directory = corrupt_root->join("wal");
    {
        auto storage = WalSegmentStorage::open(corrupt_directory, storage_options(true));
        ASSERT_TRUE(storage);
        const std::array<std::byte, 1> marker{std::byte{0x01}};
        ASSERT_TRUE(storage->write_all_at(0, marker));
        ASSERT_TRUE(storage->sync());
    }
    const auto corrupt_reader = WalReader::open(corrupt_directory, reader_options());
    ASSERT_FALSE(corrupt_reader);
    EXPECT_EQ(corrupt_reader.error().code(), base::ErrorCode::kCorruption);
}

}  // namespace
}  // namespace db::wal
