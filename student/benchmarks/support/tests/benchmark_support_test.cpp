#include <cstddef>
#include <filesystem>

#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/benchmark/benchmark_support.hpp"
#include "db/storage/page.hpp"

namespace db::benchmark_support {
namespace {

TEST(BenchmarkDeterministicRandomTest, RepeatsASeededSequenceWithinBounds) {
    DeterministicRandom first{42U};
    DeterministicRandom second{42U};
    for (std::size_t index = 0U; index < 128U; ++index) {
        EXPECT_EQ(first.next_u64(), second.next_u64());
        EXPECT_LT(first.uniform(17U), 17U);
        EXPECT_LT(second.uniform(17U), 17U);
    }
    EXPECT_EQ(first.uniform(0U), 0U);
}

TEST(BenchmarkDirectoryTest, MoveOwnershipCleansTheExactTemporaryDirectory) {
    std::filesystem::path path;
    {
        auto directory = BenchmarkDirectory::create("support-test");
        ASSERT_TRUE(directory) << directory.error().to_string();
        path = directory->path();
        ASSERT_TRUE(std::filesystem::is_directory(path));
        auto moved = std::move(directory).value();
        EXPECT_EQ(moved.path(), path);
    }
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(CountingMemoryDiskTest, PersistsPagesAndReportsOperationCounts) {
    CountingMemoryDisk disk;
    const auto relation_id = base::RelationId{901U};
    auto address = disk.allocate_page(relation_id, storage::PageKind::kHeap);
    ASSERT_TRUE(address) << address.error().to_string();
    auto page = disk.read_page(address.value());
    ASSERT_TRUE(page) << page.error().to_string();
    auto marker = page->mutable_view(256U, 1U);
    ASSERT_TRUE(marker) << marker.error().to_string();
    marker->front() = std::byte{0x5A};
    ASSERT_TRUE(disk.write_page(address.value(), page.value()));
    ASSERT_TRUE(disk.sync_relation(relation_id));
    auto persisted = disk.read_page(address.value());
    ASSERT_TRUE(persisted) << persisted.error().to_string();
    auto persisted_marker = persisted->view(256U, 1U);
    ASSERT_TRUE(persisted_marker) << persisted_marker.error().to_string();
    EXPECT_EQ(persisted_marker->front(), std::byte{0x5A});
    EXPECT_EQ(
        disk.counters(),
        (DiskCounters{.reads = 2U, .writes = 1U, .allocations = 1U, .syncs = 1U})
    );

    disk.reset_counters();
    EXPECT_EQ(disk.counters(), DiskCounters{});
    ASSERT_TRUE(disk.remove_relation(relation_id));
    EXPECT_EQ(disk.page_count(relation_id).value(), 0U);
}

}  // namespace
}  // namespace db::benchmark_support
