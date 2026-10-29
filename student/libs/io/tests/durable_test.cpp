#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/io/durable.hpp"
#include "db/io/posix.hpp"
#include "db/test_support/test_directory.hpp"

namespace db::io {
namespace {

auto bytes(const std::string_view text) -> base::Bytes {
    return std::as_bytes(std::span{text.data(), text.size()});
}

auto read_text(const std::filesystem::path& path, const std::size_t size) -> std::string {
    auto file = PosixFile::open(path);
    EXPECT_TRUE(file);
    std::string value(size, '\0');
    EXPECT_TRUE(file->read_exact_at(0, std::as_writable_bytes(std::span{value})));
    return value;
}

TEST(DurableFileTest, PublishesNewCompleteFileWithoutReplacingAnExistingName) {
    auto root = test_support::TestDirectory::create("publish-new-file");
    ASSERT_TRUE(root);
    const auto parent = root->join("wal/nested");
    const auto path = parent / "0000000000000000.wal";

    ASSERT_TRUE(publish_new_file(path, bytes("header")));
    EXPECT_EQ(read_text(path, 6U), "header");

    const auto collision = publish_new_file(path, bytes("replacement"));
    ASSERT_FALSE(collision);
    EXPECT_EQ(collision.error().code(), base::ErrorCode::kAlreadyExists);
    EXPECT_EQ(read_text(path, 6U), "header");
    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
        EXPECT_EQ(entry.path().filename(), path.filename());
    }
}

TEST(DurableFileTest, TransfersTheSyncedHandleWithoutReopeningThePublishedName) {
    auto root = test_support::TestDirectory::create("publish-new-open-file");
    ASSERT_TRUE(root);
    const auto parent = root->join("wal/nested");
    const auto path = parent / "0000000000000001.wal";

    auto published = publish_new_file_open(path, bytes("header"));
    ASSERT_TRUE(published);
    EXPECT_TRUE(published->is_open());
    std::string contents(6U, '\0');
    ASSERT_TRUE(published->read_exact_at(0U, std::as_writable_bytes(std::span{contents})));
    EXPECT_EQ(contents, "header");

    auto collision = publish_new_file_open(path, bytes("replacement"));
    ASSERT_FALSE(collision);
    EXPECT_EQ(collision.error().code(), base::ErrorCode::kAlreadyExists);
    EXPECT_EQ(read_text(path, 6U), "header");
    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
        EXPECT_EQ(entry.path().filename(), path.filename());
    }
    ASSERT_TRUE(published->close());
}

TEST(DurableFileTest, RejectsInvalidNewFilePublicationPaths) {
    auto root = test_support::TestDirectory::create("publish-new-file-errors");
    ASSERT_TRUE(root);
    EXPECT_FALSE(publish_new_file({}, bytes("value")));
    EXPECT_FALSE(publish_new_file(root->path().root_path(), bytes("value")));
}

}  // namespace
}  // namespace db::io
