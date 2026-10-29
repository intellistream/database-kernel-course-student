#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/io/durable.hpp"
#include "db/io/posix.hpp"
#include "db/test_support/test_directory.hpp"

#include "detail/file_layout.hpp"

namespace db::storage {
namespace {

using detail::NativeFile;
using detail::RelationFileLayout;

auto bytes(const std::string_view text) -> base::Bytes {
    return std::as_bytes(std::span{text.data(), text.size()});
}

auto read_text(const std::filesystem::path& path, const std::size_t size) -> std::string {
    NativeFile file{std::fopen(path.c_str(), "rb")};
    EXPECT_NE(file.get(), nullptr);
    if (file == nullptr) {
        return {};
    }
    std::string value(size, '\0');
    EXPECT_EQ(std::fread(value.data(), 1, value.size(), file.get()), value.size());
    return value;
}

auto write_at(std::FILE* const file, const std::uint64_t offset, const base::Bytes input) -> bool {
    if (::fseeko(file, static_cast<off_t>(offset), SEEK_SET) != 0) {
        return false;
    }
    if (std::fwrite(input.data(), 1, input.size(), file) != input.size()) {
        return false;
    }
    return std::fflush(file) == 0;
}

auto read_at(std::FILE* const file, const std::uint64_t offset, base::MutableBytes output) -> bool {
    if (::fseeko(file, static_cast<off_t>(offset), SEEK_SET) != 0) {
        return false;
    }
    return std::fread(output.data(), 1, output.size(), file) == output.size();
}

template <typename Result>
void expect_unavailable(const Result& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inactive(RelationFileLayout& layout) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(layout.valid());
    EXPECT_TRUE(layout.directory().empty());
    expect_unavailable(layout.relation_path(base::RelationId{1}));
    expect_unavailable(layout.segment_path(base::RelationId{1}, 0));
    expect_unavailable(layout.create_segment(base::RelationId{1}, 0));
    expect_unavailable(layout.open_segment(base::RelationId{1}, 0));
    expect_unavailable(layout.remove_segment(base::RelationId{1}, 0, true));
    expect_unavailable(layout.publish_segment(base::RelationId{1}, 0, bytes("blocked")));
    expect_unavailable(layout.sync());
}

TEST(RelationFileLayoutTest, UsesStableRelationAndSegmentNames) {
    auto root = test_support::TestDirectory::create("relation-layout-names");
    ASSERT_TRUE(root);
    auto layout = RelationFileLayout::create(root.value().join("relations"));
    ASSERT_TRUE(layout);

    EXPECT_EQ(layout.value().relation_path(base::RelationId{42}).value().filename(), "42.tbl");
    EXPECT_EQ(layout.value().segment_path(base::RelationId{42}, 0).value().filename(), "42.tbl");
    EXPECT_EQ(
        layout.value().segment_path(base::RelationId{42}, 0xABC).value().filename(),
        "42.tbl.0000000000000ABC"
    );
    EXPECT_EQ(layout.value().relation_path(base::kInvalidRelationId).value().filename(), "0.tbl");
    EXPECT_EQ(
        layout.value().segment_path(base::kInvalidRelationId, 1).value().filename(),
        "0.tbl.0000000000000001"
    );
}

TEST(RelationFileLayoutTest, CreatesOpensSyncsAndRemovesSegments) {
    auto root = test_support::TestDirectory::create("relation-layout-lifecycle");
    ASSERT_TRUE(root);
    const auto directory = root.value().join("nested/relations");
    auto layout = RelationFileLayout::create(directory);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout.value().directory(), directory);

    auto created = layout.value().create_segment(base::RelationId{7}, 0);
    ASSERT_TRUE(created);
    EXPECT_TRUE(write_at(created.value().get(), 0, bytes("relation")));
    EXPECT_EQ(::fdatasync(::fileno(created.value().get())), 0);
    EXPECT_FALSE(layout.value().create_segment(base::RelationId{7}, 0));

    auto reopened = layout.value().open_segment(base::RelationId{7}, 0, io::FileAccess::kReadOnly);
    ASSERT_TRUE(reopened);
    std::array<char, 8> output{};
    EXPECT_TRUE(read_at(reopened.value().get(), 0, std::as_writable_bytes(std::span{output})));
    EXPECT_EQ(std::string_view(output.data(), output.size()), "relation");
    EXPECT_TRUE(layout.value().sync());

    EXPECT_TRUE(layout.value().remove_segment(base::RelationId{7}, 0));
    EXPECT_FALSE(layout.value().open_segment(base::RelationId{7}, 0));
    EXPECT_FALSE(layout.value().remove_segment(base::RelationId{7}, 0));
    EXPECT_TRUE(layout.value().remove_segment(base::RelationId{7}, 0, true));
}

TEST(RelationFileLayoutTest, PublishesCompleteImagesOverExistingSegments) {
    auto root = test_support::TestDirectory::create("relation-layout-publish");
    ASSERT_TRUE(root);
    auto layout = RelationFileLayout::create(root.value().join("relations"));
    ASSERT_TRUE(layout);
    const auto path = layout.value().segment_path(base::RelationId{9}, 3).value();

    EXPECT_TRUE(layout.value().publish_segment(base::RelationId{9}, 3, bytes("old")));
    EXPECT_EQ(read_text(path, 3), "old");
    EXPECT_TRUE(layout.value().publish_segment(base::RelationId{9}, 3, bytes("new image")));
    EXPECT_EQ(read_text(path, 9), "new image");
}

TEST(RelationFileLayoutTest, MovingTransfersNamespaceAndMakesSourceInert) {
    auto root = test_support::TestDirectory::create("relation-layout-move");
    ASSERT_TRUE(root);
    const auto first_directory = root->join("first");
    const auto second_directory = root->join("second");
    auto first = RelationFileLayout::create(first_directory);
    auto second = RelationFileLayout::create(second_directory);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    RelationFileLayout moved{std::move(first).value()};
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.directory(), first_directory);
    // NOLINTNEXTLINE(bugprone-use-after-move) - verifies the documented inert source.
    expect_inactive(first.value());

    moved = std::move(second).value();
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.directory(), second_directory);
    EXPECT_EQ(moved.relation_path(base::RelationId{7}).value().parent_path(), second_directory);
    // NOLINTNEXTLINE(bugprone-use-after-move) - verifies the documented inert source.
    expect_inactive(second.value());
}

TEST(DurableFileTest, CreatesParentsReplacesAndLeavesNoTemporaryFile) {
    auto root = test_support::TestDirectory::create("atomic-replace");
    ASSERT_TRUE(root);
    const auto parent = root.value().join("metadata/nested");
    const auto path = parent / "control";

    EXPECT_TRUE(io::atomic_replace(path, bytes("first")));
    EXPECT_TRUE(io::atomic_replace(path, bytes("second")));
    EXPECT_EQ(read_text(path, 6), "second");
    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
        EXPECT_EQ(entry.path().filename(), "control");
    }
}

TEST(DurableFileTest, ValidatesPathsAndDurablyRemoves) {
    auto root = test_support::TestDirectory::create("durable-remove");
    ASSERT_TRUE(root);
    const auto path = root.value().join("entry");
    EXPECT_FALSE(io::atomic_replace({}, bytes("value")));
    EXPECT_FALSE(io::atomic_replace(root.value().path().root_path(), bytes("value")));
    EXPECT_FALSE(io::remove_file_durable(path));
    EXPECT_TRUE(io::atomic_replace(path, {}));
    EXPECT_TRUE(io::remove_file_durable(path));
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(io::remove_file_durable(path, true));
}

TEST(RelationFileLayoutTest, RejectsMissingAndInvalidDirectories) {
    auto root = test_support::TestDirectory::create("relation-layout-errors");
    ASSERT_TRUE(root);
    EXPECT_FALSE(RelationFileLayout::open(root.value().join("missing")));
    EXPECT_FALSE(RelationFileLayout::open({}));
    EXPECT_FALSE(RelationFileLayout::create({}));
}

}  // namespace
}  // namespace db::storage
