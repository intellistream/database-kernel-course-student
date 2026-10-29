#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/io/posix.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/control_store.hpp"
#include "db/wal/format.hpp"

namespace db::wal {
namespace {

inline constexpr std::uint64_t kSystemIdentifier = 0x1020'3040'5060'7080ULL;

auto initial_image() -> WalControlImage {
    return WalControlImage{
        .system_identifier = kSystemIdentifier,
        .generation = kInitialWalControlGeneration,
    };
}

auto successor(const WalControlImage& current) -> WalControlImage {
    auto next = current;
    ++next.generation;
    next.checkpoint_lsn = base::Lsn{1024};
    next.redo_lsn = base::Lsn{512};
    next.next_xid = base::Xid{77};
    next.latest_completed_xid = base::Xid{75};
    next.next_object_id = 11;
    next.state = DatabaseState::kInProduction;
    return next;
}

auto read_bytes(const std::filesystem::path& path) -> WalControlFileBytes {
    auto file = io::PosixFile::open(path);
    EXPECT_TRUE(file);
    WalControlFileBytes bytes{};
    EXPECT_TRUE(file->read_exact_at(0, bytes));
    return bytes;
}

void write_bytes(const std::filesystem::path& path, const base::Bytes bytes) {
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

auto sealed_bytes(WalControlImage image) -> WalControlFileBytes {
    image.checksum = 0;
    auto bytes = encode_control_image(image);
    EXPECT_TRUE(bytes);
    EXPECT_TRUE(base::seal_crc32c(bytes.value(), kWalControlChecksumOffset));
    return bytes.value();
}

TEST(WalControlStoreTest, CreatesSealedAuthoritativeAndShadowImages) {
    auto root = test_support::TestDirectory::create("wal-control-create");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    const auto created = WalControlStore::create(directory, initial_image());
    ASSERT_TRUE(created);
    EXPECT_EQ(created->directory(), directory);
    EXPECT_EQ(created->current().generation, 1U);
    EXPECT_FALSE(created->has_staged_update());
    EXPECT_EQ(created->staged().error().code(), base::ErrorCode::kNotFound);

    const auto authoritative = read_bytes(created->current_path());
    const auto shadow = read_bytes(created->staged_path());
    EXPECT_EQ(authoritative, shadow);
    EXPECT_TRUE(base::verify_crc32c(authoritative, kWalControlChecksumOffset));
    EXPECT_NE(created->current().checksum, 0U);

    const auto reopened = WalControlStore::open(directory);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->current(), created->current());
    EXPECT_FALSE(reopened->has_staged_update());
    EXPECT_FALSE(WalControlStore::create(directory, initial_image()));
}

TEST(WalControlStoreTest, StagingDoesNotPublishAcrossReopen) {
    auto root = test_support::TestDirectory::create("wal-control-stage");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    auto store = WalControlStore::create(directory, initial_image());
    ASSERT_TRUE(store);
    auto next = successor(store->current());
    ASSERT_TRUE(store->stage(next));
    EXPECT_EQ(store->current().generation, 1U);
    ASSERT_TRUE(store->has_staged_update());
    EXPECT_EQ(store->staged()->generation, 2U);

    auto reopened = WalControlStore::open(directory);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->current().generation, 1U);
    EXPECT_TRUE(reopened->has_staged_update());
    EXPECT_EQ(reopened->staged()->state, DatabaseState::kInProduction);

    ASSERT_TRUE(reopened->publish_staged());
    EXPECT_EQ(reopened->current().generation, 2U);
    EXPECT_FALSE(reopened->has_staged_update());
    const auto published = WalControlStore::open(directory);
    ASSERT_TRUE(published);
    EXPECT_EQ(published->current().generation, 2U);
    EXPECT_FALSE(published->has_staged_update());
}

TEST(WalControlStoreTest, PublishConvenienceAdvancesExactlyOneGeneration) {
    auto root = test_support::TestDirectory::create("wal-control-publish");
    ASSERT_TRUE(root);
    auto store = WalControlStore::create(root->join("wal"), initial_image());
    ASSERT_TRUE(store);
    const auto next = successor(store->current());
    ASSERT_TRUE(store->publish(next));
    EXPECT_EQ(store->current().generation, 2U);
    EXPECT_EQ(store->current().system_identifier, kSystemIdentifier);
    EXPECT_EQ(store->current().checkpoint_lsn, base::Lsn{1024});
    EXPECT_EQ(store->current().redo_lsn, base::Lsn{512});
    EXPECT_EQ(store->current().next_xid, base::Xid{77});
    EXPECT_EQ(store->current().latest_completed_xid, base::Xid{75});
    EXPECT_EQ(store->current().next_object_id, 11U);
    EXPECT_EQ(store->current().state, DatabaseState::kInProduction);
    EXPECT_EQ(read_bytes(store->current_path()), read_bytes(store->staged_path()));
}

TEST(WalControlStoreTest, RejectsInvalidCreateAndGenerationTransitions) {
    auto root = test_support::TestDirectory::create("wal-control-invalid");
    ASSERT_TRUE(root);
    auto invalid_initial = initial_image();
    invalid_initial.generation = 0;
    EXPECT_FALSE(WalControlStore::create(root->join("zero-generation"), invalid_initial));
    EXPECT_FALSE(WalControlStore::create({}, initial_image()));

    auto store = WalControlStore::create(root->join("wal"), initial_image());
    ASSERT_TRUE(store);
    auto next = successor(store->current());
    next.generation = 3;
    EXPECT_FALSE(store->stage(next));
    next.generation = 2;
    next.system_identifier += 1U;
    EXPECT_FALSE(store->stage(next));
    EXPECT_FALSE(store->publish_staged());

    auto maximum = initial_image();
    maximum.generation = std::numeric_limits<std::uint64_t>::max();
    write_bytes(store->current_path(), sealed_bytes(maximum));
    write_bytes(store->staged_path(), sealed_bytes(maximum));
    auto exhausted = WalControlStore::open(store->directory());
    ASSERT_TRUE(exhausted);
    next = maximum;
    EXPECT_FALSE(exhausted->stage(next));
}

TEST(WalControlStoreTest, RejectsCorruptMissingAndUnsupportedAuthoritativeImages) {
    auto root = test_support::TestDirectory::create("wal-control-corrupt");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    auto store = WalControlStore::create(directory, initial_image());
    ASSERT_TRUE(store);
    auto bytes = read_bytes(store->current_path());
    bytes[40] ^= std::byte{1};
    write_bytes(store->current_path(), bytes);
    EXPECT_EQ(WalControlStore::open(directory).error().code(), base::ErrorCode::kCorruption);

    bytes = sealed_bytes(initial_image());
    base::LittleEndianWriter version_writer{base::MutableBytes{bytes}.subspan(8, 2)};
    ASSERT_TRUE(version_writer.write_u16(kWalFormatVersion + 1U));
    ASSERT_TRUE(base::seal_crc32c(bytes, kWalControlChecksumOffset));
    write_bytes(store->current_path(), bytes);
    EXPECT_EQ(WalControlStore::open(directory).error().code(), base::ErrorCode::kUnsupported);

    ASSERT_TRUE(io::remove_file(store->current_path(), false));
    EXPECT_EQ(WalControlStore::open(directory).error().code(), base::ErrorCode::kNotFound);
}

TEST(WalControlStoreTest, RejectsInvalidStagedRelationshipsWithoutFallback) {
    auto root = test_support::TestDirectory::create("wal-control-staged-corrupt");
    ASSERT_TRUE(root);
    const auto directory = root->join("wal");
    auto store = WalControlStore::create(directory, initial_image());
    ASSERT_TRUE(store);
    auto staged = successor(store->current());
    staged.system_identifier += 1U;
    write_bytes(store->staged_path(), sealed_bytes(staged));
    EXPECT_EQ(WalControlStore::open(directory).error().code(), base::ErrorCode::kCorruption);

    staged = successor(store->current());
    staged.generation = 3;
    write_bytes(store->staged_path(), sealed_bytes(staged));
    EXPECT_EQ(WalControlStore::open(directory).error().code(), base::ErrorCode::kCorruption);

    staged = store->current();
    staged.state = DatabaseState::kStartingUp;
    write_bytes(store->staged_path(), sealed_bytes(staged));
    EXPECT_EQ(WalControlStore::open(directory).error().code(), base::ErrorCode::kCorruption);
}

TEST(WalControlStoreTest, PublishRevalidatesStagedBytes) {
    auto root = test_support::TestDirectory::create("wal-control-revalidate");
    ASSERT_TRUE(root);
    auto store = WalControlStore::create(root->join("wal"), initial_image());
    ASSERT_TRUE(store);
    ASSERT_TRUE(store->stage(successor(store->current())));
    auto bytes = read_bytes(store->staged_path());
    bytes[48] ^= std::byte{1};
    write_bytes(store->staged_path(), bytes);
    EXPECT_FALSE(store->publish_staged());
    EXPECT_EQ(store->current().generation, 1U);
}

}  // namespace
}  // namespace db::wal
