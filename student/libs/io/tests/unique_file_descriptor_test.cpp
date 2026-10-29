#include <array>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <utility>

#include <gtest/gtest.h>

#include "unique_file_descriptor.hpp"

namespace db::io::internal {
namespace {

auto descriptor_is_open(const int descriptor) -> bool {
    errno = 0;
    // POSIX exposes fcntl(2) as a variadic C API.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    return ::fcntl(descriptor, F_GETFD) != -1 || errno != EBADF;
}

TEST(UniqueFileDescriptorTest, ClosesOwnedDescriptorAtScopeExit) {
    std::array<int, 2> descriptors{-1, -1};
    ASSERT_EQ(::pipe(descriptors.data()), 0);
    const UniqueFileDescriptor peer{descriptors[1]};
    ASSERT_TRUE(peer.is_valid());
    const int guarded_descriptor = descriptors[0];

    {
        const UniqueFileDescriptor guarded{guarded_descriptor};
        EXPECT_TRUE(guarded.is_valid());
        EXPECT_EQ(guarded.get(), guarded_descriptor);
        EXPECT_TRUE(descriptor_is_open(guarded_descriptor));
    }

    EXPECT_FALSE(descriptor_is_open(guarded_descriptor));
}

TEST(UniqueFileDescriptorTest, TransfersAndCanReleaseOwnership) {
    std::array<int, 2> descriptors{-1, -1};
    ASSERT_EQ(::pipe(descriptors.data()), 0);
    UniqueFileDescriptor source{descriptors[0]};
    const UniqueFileDescriptor peer{descriptors[1]};
    ASSERT_TRUE(peer.is_valid());

    UniqueFileDescriptor destination{std::move(source)};
    // Inspecting the documented empty source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(source.is_valid());
    ASSERT_TRUE(destination.is_valid());

    const int released = destination.release();
    EXPECT_FALSE(destination.is_valid());
    ASSERT_TRUE(descriptor_is_open(released));
    EXPECT_EQ(::close(released), 0);
}

}  // namespace
}  // namespace db::io::internal
