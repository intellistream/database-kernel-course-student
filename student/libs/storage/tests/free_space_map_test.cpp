#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/io/posix.hpp"
#include "db/storage/free_space_map.hpp"
#include "db/test_support/test_directory.hpp"

#include "detail/durable_metadata.hpp"

namespace db::storage {
namespace {

using detail::kDurableMetadataHeaderSize;
using detail::load_metadata;
using detail::MetadataReadOptions;
using detail::publish_metadata;

template <typename Result>
void expect_unavailable(const Result& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inactive(FreeSpaceMap& map) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(map.valid());
    EXPECT_EQ(map.tracked_pages(), 0U);
    EXPECT_EQ(map.generation(), 0U);
    EXPECT_FALSE(map.is_persistent());
    EXPECT_FALSE(map.free_space_class(base::PageId{0}).has_value());

    expect_unavailable(map.find_page(1U));
    expect_unavailable(map.record_and_find(base::PageId{0}, 1U, 1U));
    expect_unavailable(map.update(base::PageId{0}, 1U));
    expect_unavailable(map.persist());
}

TEST(FreeSpaceMapTest, ClassifiesAndFindsLeftmostUsablePage) {
    FreeSpaceMap map;
    ASSERT_TRUE(map.update(base::PageId{0}, kMaximumPageFreeSpace));
    ASSERT_TRUE(map.update(base::PageId{1}, 200));
    ASSERT_TRUE(map.update(base::PageId{2}, 100));
    ASSERT_TRUE(map.update(base::PageId{3}, 0));

    EXPECT_EQ(map.free_space_class(base::PageId{0}), 255);
    EXPECT_EQ(map.free_space_class(base::PageId{3}), 0);
    EXPECT_EQ(map.find_page(kMaximumPageFreeSpace).value(), base::PageId{0});
    EXPECT_EQ(map.find_page(150).value(), base::PageId{0});
    EXPECT_FALSE(map.find_page(kMaximumPageFreeSpace + 1U));
}

TEST(FreeSpaceMapTest, UpdatesBubbleUpAndGrowthPreservesCategories) {
    FreeSpaceMap map;
    ASSERT_TRUE(map.update(base::PageId{0}, 500));
    ASSERT_TRUE(map.update(base::PageId{1}, 300));
    const auto page_zero_class = map.free_space_class(base::PageId{0});
    const auto page_one_class = map.free_space_class(base::PageId{1});

    ASSERT_TRUE(map.update(base::PageId{100}, kMaximumPageFreeSpace));
    EXPECT_EQ(map.tracked_pages(), 101U);
    EXPECT_EQ(map.free_space_class(base::PageId{0}), page_zero_class);
    EXPECT_EQ(map.free_space_class(base::PageId{1}), page_one_class);
    EXPECT_EQ(map.free_space_class(base::PageId{10}), 0);
    EXPECT_EQ(map.find_page(kMaximumPageFreeSpace).value(), base::PageId{100});

    ASSERT_TRUE(map.update(base::PageId{100}, 0));
    EXPECT_EQ(map.find_page(400).value(), base::PageId{0});
}

TEST(FreeSpaceMapTest, EmptyMapAndStaleHintRepairAreDeterministic) {
    FreeSpaceMap map;
    EXPECT_FALSE(map.find_page(0).value().has_value());
    ASSERT_TRUE(map.update(base::PageId{0}, kMaximumPageFreeSpace));
    ASSERT_TRUE(map.update(base::PageId{1}, 300));
    EXPECT_EQ(map.record_and_find(base::PageId{0}, 100, 200).value(), base::PageId{1});
}

TEST(FreeSpaceMapTest, PersistsGenerationsAndReopensCompactCategories) {
    auto root = test_support::TestDirectory::create("free-space-map-roundtrip");
    ASSERT_TRUE(root);
    const auto path = root.value().join("fsm/relation-7.fsm");
    auto created = FreeSpaceMap::create(path);
    ASSERT_TRUE(created);
    EXPECT_TRUE(created.value().is_persistent());
    EXPECT_EQ(created.value().generation(), 1U);
    ASSERT_TRUE(created.value().update(base::PageId{0}, 300));
    ASSERT_TRUE(created.value().update(base::PageId{70}, kMaximumPageFreeSpace));
    ASSERT_TRUE(created.value().persist());
    EXPECT_EQ(created.value().generation(), 2U);

    auto reopened = FreeSpaceMap::open(path);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened.value().generation(), 2U);
    EXPECT_EQ(reopened.value().tracked_pages(), 71U);
    EXPECT_EQ(reopened.value().find_page(kMaximumPageFreeSpace).value(), base::PageId{70});
    EXPECT_EQ(
        reopened.value().free_space_class(base::PageId{0}),
        created.value().free_space_class(base::PageId{0})
    );
    ASSERT_TRUE(reopened.value().update(base::PageId{70}, 0));
    ASSERT_TRUE(reopened.value().persist());
    EXPECT_EQ(reopened.value().generation(), 3U);
}

TEST(FreeSpaceMapTest, RejectsInvalidRangesAndVolatilePersistence) {
    FreeSpaceMap map;
    EXPECT_FALSE(map.update(base::kInvalidPageId, 1));
    EXPECT_FALSE(map.update(base::PageId{kMaximumFreeSpaceMapPages}, 1));
    EXPECT_FALSE(map.update(base::PageId{0}, kMaximumPageFreeSpace + 1U));
    EXPECT_FALSE(map.find_page(kMaximumPageFreeSpace + 1U));
    EXPECT_FALSE(map.persist());
    EXPECT_FALSE(map.free_space_class(base::kInvalidPageId).has_value());

    auto root = test_support::TestDirectory::create("free-space-map-existing");
    ASSERT_TRUE(root);
    const auto path = root.value().join("state.fsm");
    ASSERT_TRUE(FreeSpaceMap::create(path));
    const auto duplicate = FreeSpaceMap::create(path);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kAlreadyExists);
}

TEST(FreeSpaceMapTest, RejectsCorruptionAndUnsupportedVersion) {
    auto root = test_support::TestDirectory::create("free-space-map-corruption");
    ASSERT_TRUE(root);
    const auto corrupt_path = root.value().join("corrupt.fsm");
    auto map = FreeSpaceMap::create(corrupt_path);
    ASSERT_TRUE(map);
    ASSERT_TRUE(map.value().update(base::PageId{0}, 100));
    ASSERT_TRUE(map.value().persist());
    auto file = io::PosixFile::open(
        corrupt_path,
        io::FileOpenOptions{.access = io::FileAccess::kReadWrite}
    );
    ASSERT_TRUE(file);
    const std::array corrupt_byte{std::byte{0xFF}};
    ASSERT_TRUE(file.value().write_all_at(kDurableMetadataHeaderSize + 16U, corrupt_byte));
    const auto corrupt = FreeSpaceMap::open(corrupt_path);
    ASSERT_FALSE(corrupt);
    EXPECT_EQ(corrupt.error().code(), base::ErrorCode::kCorruption);

    const auto version_path = root.value().join("version.fsm");
    ASSERT_TRUE(FreeSpaceMap::create(version_path));
    const auto metadata = load_metadata(
        version_path,
        MetadataReadOptions{kFreeSpaceMapMagic, kFreeSpaceMapVersion, 1024}
    );
    ASSERT_TRUE(metadata);
    ASSERT_TRUE(publish_metadata(
        version_path,
        kFreeSpaceMapMagic,
        kFreeSpaceMapVersion + 1U,
        metadata.value().generation,
        metadata.value().payload
    ));
    const auto unsupported = FreeSpaceMap::open(version_path);
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code(), base::ErrorCode::kUnsupported);
}

TEST(FreeSpaceMapTest, MoveTransfersStateAndLeavesTheSourceInert) {
    FreeSpaceMap source;
    ASSERT_TRUE(source.update(base::PageId{7}, kMaximumPageFreeSpace));

    FreeSpaceMap moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.tracked_pages(), 8U);
    EXPECT_EQ(moved.find_page(kMaximumPageFreeSpace).value(), base::PageId{7});
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive(source);

    FreeSpaceMap assigned;
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    EXPECT_EQ(assigned.find_page(kMaximumPageFreeSpace).value(), base::PageId{7});
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive(moved);
}

TEST(FreeSpaceMapTest, ConcurrentReadersAndDisjointUpdatesRemainConsistent) {
    FreeSpaceMap map;
    constexpr std::size_t k_threads = 8;
    constexpr std::size_t k_pages_per_thread = 64;
    std::atomic<bool> start{false};
    std::atomic<std::size_t> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(k_threads);
    for (std::size_t thread = 0; thread < k_threads; ++thread) {
        threads.emplace_back([&, thread] {
            while (!start.load()) {
                std::this_thread::yield();
            }
            for (std::size_t offset = 0; offset < k_pages_per_thread; ++offset) {
                const auto raw_page = thread * k_pages_per_thread + offset;
                const auto free_bytes =
                    offset == 0 ? kMaximumPageFreeSpace : static_cast<std::uint16_t>(offset);
                if (!map.update(base::PageId{static_cast<std::uint32_t>(raw_page)}, free_bytes)
                    || !map.find_page(free_bytes)) {
                    failures.fetch_add(1);
                }
            }
        });
    }
    start.store(true);
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(failures.load(), 0U);
    EXPECT_EQ(map.tracked_pages(), k_threads * k_pages_per_thread);
    EXPECT_EQ(map.find_page(kMaximumPageFreeSpace).value(), base::PageId{0});
}

}  // namespace
}  // namespace db::storage
