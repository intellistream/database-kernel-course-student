#include <utility>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/io/file_lock.hpp"
#include "db/test_support/test_directory.hpp"

namespace db::io {
namespace {

TEST(ExclusiveFileLockTest, RejectsContentionAndReleasesByExplicitClose) {
    auto directory = test_support::TestDirectory::create("exclusive-file-lock");
    ASSERT_TRUE(directory);
    const auto path = directory->join("database.lock");

    auto first = ExclusiveFileLock::try_acquire(path);
    ASSERT_TRUE(first) << first.error().to_string();
    EXPECT_TRUE(first->is_locked());

    auto second = ExclusiveFileLock::try_acquire(path);
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_TRUE(second.error().has_system_error());

    ExclusiveFileLock moved{std::move(first).value()};
    EXPECT_TRUE(moved.is_locked());
    ASSERT_TRUE(moved.release());
    EXPECT_FALSE(moved.is_locked());
    EXPECT_TRUE(moved.release());

    auto after_release = ExclusiveFileLock::try_acquire(path);
    ASSERT_TRUE(after_release) << after_release.error().to_string();
}

TEST(ExclusiveFileLockTest, RejectsInvalidOpenArguments) {
    const auto empty_path = ExclusiveFileLock::try_acquire({});
    ASSERT_FALSE(empty_path);
    EXPECT_EQ(empty_path.error().code(), base::ErrorCode::kInvalidArgument);

    auto directory = test_support::TestDirectory::create("exclusive-file-lock-permissions");
    ASSERT_TRUE(directory);
    const auto invalid_permissions =
        ExclusiveFileLock::try_acquire(directory->join("database.lock"), 01000U);
    ASSERT_FALSE(invalid_permissions);
    EXPECT_EQ(invalid_permissions.error().code(), base::ErrorCode::kInvalidArgument);
}

}  // namespace
}  // namespace db::io
