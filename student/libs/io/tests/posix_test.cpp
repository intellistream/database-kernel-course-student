#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/io/posix.hpp"
#include "db/test_support/test_directory.hpp"

namespace db::io {
namespace {

auto as_bytes(const std::string_view text) -> base::Bytes {
    return std::as_bytes(std::span{text.data(), text.size()});
}

auto as_writable_bytes(std::span<char> output) -> base::MutableBytes {
    return std::as_writable_bytes(output);
}

template <typename Value>
void expect_unavailable(const base::Result<Value>& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_closed(const PosixFile& file) {
    // This helper also validates the class's documented moved-from state.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(file.is_open());
    EXPECT_EQ(file.native_handle(), -1);

    std::array<std::byte, 1> byte{};
    expect_unavailable(file.read_at(0, byte));
    expect_unavailable(file.read_exact_at(0, byte));
    expect_unavailable(file.write_at(0, byte));
    expect_unavailable(file.write_all_at(0, byte));
    expect_unavailable(file.size());
    expect_unavailable(file.truncate(0));
    expect_unavailable(file.sync());
    expect_unavailable(file.sync_data());
}

void expect_closed(const PosixDirectory& directory) {
    // This helper also validates the class's documented moved-from state.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(directory.is_open());
    EXPECT_EQ(directory.native_handle(), -1);
    expect_unavailable(directory.sync());
}

TEST(PosixFileTest, WritesReadsAndReportsSizeAtExplicitOffsets) {
    auto directory = test_support::TestDirectory::create("posix-file-read-write");
    ASSERT_TRUE(directory);
    const auto path = directory.value().join("relation.dat");
    auto file = PosixFile::open(
        path,
        FileOpenOptions{.access = FileAccess::kReadWrite, .create = true, .exclusive = true}
    );
    ASSERT_TRUE(file);

    EXPECT_TRUE(file.value().write_all_at(4, as_bytes("data")));
    EXPECT_TRUE(file.value().write_all_at(0, as_bytes("head")));
    EXPECT_EQ(file.value().size().value(), 8U);

    std::array<char, 8> output{};
    EXPECT_TRUE(file.value().read_exact_at(0, as_writable_bytes(output)));
    EXPECT_EQ(std::string_view(output.data(), output.size()), "headdata");
    EXPECT_TRUE(file.value().sync_data());
    EXPECT_TRUE(file.value().sync());
}

TEST(PosixFileTest, SupportsPartialReadTruncateAndSparseExtension) {
    auto directory = test_support::TestDirectory::create("posix-file-resize");
    ASSERT_TRUE(directory);
    auto file = PosixFile::open(
        directory.value().join("resize.dat"),
        FileOpenOptions{.access = FileAccess::kReadWrite, .create = true}
    );
    ASSERT_TRUE(file);
    ASSERT_TRUE(file.value().write_all_at(0, as_bytes("abcdef")));

    std::array<char, 8> output{};
    const auto partial = file.value().read_at(4, as_writable_bytes(output));
    ASSERT_TRUE(partial);
    EXPECT_EQ(partial.value(), 2U);
    EXPECT_EQ(std::string_view(output.data(), partial.value()), "ef");
    EXPECT_FALSE(file.value().read_exact_at(4, as_writable_bytes(output)));

    EXPECT_TRUE(file.value().truncate(3));
    EXPECT_EQ(file.value().size().value(), 3U);
    EXPECT_TRUE(file.value().truncate(10));
    EXPECT_EQ(file.value().size().value(), 10U);
}

TEST(PosixFileTest, EnforcesOpenFlagsAndMapsStableErrors) {
    auto directory = test_support::TestDirectory::create("posix-file-errors");
    ASSERT_TRUE(directory);
    const auto missing = PosixFile::open(directory.value().join("missing"));
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), base::ErrorCode::kNotFound);
    EXPECT_TRUE(missing.error().has_system_error());

    const auto path = directory.value().join("exclusive");
    auto created = PosixFile::open(
        path,
        FileOpenOptions{.access = FileAccess::kReadWrite, .create = true, .exclusive = true}
    );
    ASSERT_TRUE(created);
    const auto duplicate = PosixFile::open(
        path,
        FileOpenOptions{.access = FileAccess::kReadWrite, .create = true, .exclusive = true}
    );
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kAlreadyExists);

    EXPECT_FALSE(PosixFile::open(path, FileOpenOptions{.exclusive = true}));
    EXPECT_FALSE(PosixFile::open(path, FileOpenOptions{.truncate = true}));
    EXPECT_FALSE(PosixFile::open({}, FileOpenOptions{}));
}

TEST(PosixFileTest, MovesOwnershipAndRejectsClosedOrUnrepresentableOperations) {
    auto directory = test_support::TestDirectory::create("posix-file-ownership");
    ASSERT_TRUE(directory);
    auto opened = PosixFile::open(
        directory.value().join("owned"),
        FileOpenOptions{.access = FileAccess::kReadWrite, .create = true}
    );
    ASSERT_TRUE(opened);
    PosixFile source{std::move(opened).value()};
    PosixFile moved{std::move(source)};
    // Inspecting the documented closed source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_closed(source);
    EXPECT_TRUE(moved.is_open());
    EXPECT_GE(moved.native_handle(), 0);

    EXPECT_TRUE(moved.close());
    expect_closed(moved);
    EXPECT_TRUE(moved.close());

    auto second = PosixFile::open(
        directory.value().join("offset"),
        FileOpenOptions{.access = FileAccess::kReadWrite, .create = true}
    );
    ASSERT_TRUE(second);
    std::array<std::byte, 1> byte{};
    EXPECT_FALSE(second.value().read_at(std::numeric_limits<std::uint64_t>::max(), byte));
    EXPECT_FALSE(second.value().write_at(std::numeric_limits<std::uint64_t>::max(), byte));
    EXPECT_FALSE(second.value().truncate(std::numeric_limits<std::uint64_t>::max()));
}

TEST(PosixDirectoryTest, CreatesOpensMovesAndSyncsNestedDirectories) {
    auto root = test_support::TestDirectory::create("posix-directory");
    ASSERT_TRUE(root);
    const auto nested = root.value().join("one/two/three");
    EXPECT_TRUE(db::io::create_directories(nested));
    EXPECT_TRUE(std::filesystem::is_directory(nested));
    EXPECT_TRUE(db::io::create_directories(nested));

    auto opened = PosixDirectory::open(nested);
    ASSERT_TRUE(opened);
    PosixDirectory source{std::move(opened).value()};
    PosixDirectory moved{std::move(source)};
    // Inspecting the documented closed source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_closed(source);
    EXPECT_TRUE(moved.is_open());
    EXPECT_GE(moved.native_handle(), 0);
    EXPECT_TRUE(moved.sync());
    EXPECT_TRUE(moved.close());
    expect_closed(moved);
    EXPECT_TRUE(moved.close());
}

TEST(PosixDirectoryTest, RejectsMissingNonDirectoryAndInvalidInputs) {
    auto root = test_support::TestDirectory::create("posix-directory-errors");
    ASSERT_TRUE(root);
    const auto missing = PosixDirectory::open(root.value().join("missing"));
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), base::ErrorCode::kNotFound);

    const auto file_path = root.value().join("plain-file");
    auto file = PosixFile::open(
        file_path,
        FileOpenOptions{.access = FileAccess::kReadWrite, .create = true}
    );
    ASSERT_TRUE(file);
    EXPECT_FALSE(PosixDirectory::open(file_path));
    EXPECT_FALSE(db::io::create_directories(file_path / "child"));
    EXPECT_FALSE(db::io::create_directories({}));
    EXPECT_FALSE(db::io::create_directories(root.value().path(), 01000U));
}

}  // namespace
}  // namespace db::io
