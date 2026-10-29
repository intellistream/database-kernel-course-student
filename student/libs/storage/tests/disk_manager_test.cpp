#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/io/posix.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/page.hpp"
#include "db/test_support/test_directory.hpp"

#include "detail/durable_metadata.hpp"
#include "detail/file_layout.hpp"
#include "detail/relation_segment.hpp"

namespace db::storage {
namespace {

using detail::kDurableMetadataHeaderSize;
using detail::load_metadata;
using detail::MetadataReadOptions;
using detail::publish_metadata;
using detail::RelationFileLayout;

auto bytes(const std::string_view text) -> base::Bytes {
    return std::as_bytes(std::span{text.data(), text.size()});
}

TEST(FileDiskManagerTest, AllocatesWritesReadsSyncsAndReopensRelations) {
    auto root = test_support::TestDirectory::create("disk-manager-roundtrip");
    ASSERT_TRUE(root);
    const auto path = root.value().join("relations");
    auto manager = FileDiskManager::open(path);
    ASSERT_TRUE(manager);

    const auto first = manager.value()->allocate_page(base::RelationId{7}, PageKind::kHeap);
    const auto second = manager.value()->allocate_page(base::RelationId{7}, PageKind::kHeap, 16);
    const auto other = manager.value()->allocate_page(base::RelationId{8}, PageKind::kBtree);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(other);
    EXPECT_EQ(first.value().page_id, base::PageId{0});
    EXPECT_EQ(second.value().page_id, base::PageId{1});
    EXPECT_EQ(other.value().page_id, base::PageId{0});

    auto page = manager.value()->read_page(second.value());
    ASSERT_TRUE(page);
    auto payload = page.value().mutable_view(100, 1);
    ASSERT_TRUE(payload);
    payload.value().front() = std::byte{0xAB};
    EXPECT_TRUE(page.value().set_lsn(base::Lsn{99}));
    EXPECT_TRUE(manager.value()->write_page(second.value(), page.value()));
    EXPECT_TRUE(manager.value()->sync_relation(base::RelationId{7}));
    EXPECT_TRUE(manager.value()->sync_all());
    EXPECT_EQ(manager.value()->page_count(base::RelationId{7}).value(), 2U);

    manager = FileDiskManager::open(path);
    ASSERT_TRUE(manager);
    EXPECT_EQ(manager.value()->page_count(base::RelationId{7}).value(), 2U);
    const auto reopened = manager.value()->read_page(second.value());
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened.value().view(100, 1).value().front(), std::byte{0xAB});
    EXPECT_EQ(reopened.value().lsn(), base::Lsn{99});
}

TEST(FileDiskManagerTest, ReadsAndWritesRawPagesWithoutPageValidation) {
    auto root = test_support::TestDirectory::create("disk-manager-errors");
    ASSERT_TRUE(root);
    auto manager = FileDiskManager::open(root.value().join("relations"));
    ASSERT_TRUE(manager);
    const PageAddress missing{base::RelationId{12}, base::PageId{0}};
    EXPECT_FALSE(manager.value()->read_page(missing));
    EXPECT_FALSE(manager.value()->page_count(base::RelationId{12}));

    const auto address = manager.value()->allocate_page(base::RelationId{12}, PageKind::kHeap);
    ASSERT_TRUE(address);
    EXPECT_FALSE(manager.value()->read_page(PageAddress{base::RelationId{12}, base::PageId{1}}));
    auto page = manager.value()->read_page(address.value());
    ASSERT_TRUE(page);
    ASSERT_TRUE(page.value().mutable_view(200, 1));
    EXPECT_TRUE(manager.value()->write_page(address.value(), page.value()));

    auto corrupt_page = page.value();
    auto header = corrupt_page.mutable_view(0, 1);
    ASSERT_TRUE(header);
    header->front() = std::byte{0};
    EXPECT_TRUE(manager.value()->write_page(address.value(), corrupt_page));
    auto raw = manager.value()->read_page(address.value());
    ASSERT_TRUE(raw);
    EXPECT_EQ(raw.value().bytes().front(), std::byte{0});

    RelationFileLayout layout =
        std::move(RelationFileLayout::open(root.value().join("relations"))).value();
    auto relation = layout.open_segment(base::RelationId{12}, 0);
    ASSERT_TRUE(relation);
    const std::array corrupt{std::byte{0xFF}};
    ASSERT_EQ(::fseeko(relation.value().get(), 0, SEEK_SET), 0);
    ASSERT_EQ(
        std::fwrite(corrupt.data(), 1, corrupt.size(), relation.value().get()),
        corrupt.size()
    );
    ASSERT_EQ(std::fflush(relation.value().get()), 0);
    const auto loaded = manager.value()->read_page(address.value());
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value().bytes().front(), std::byte{0xFF});
}

TEST(FileDiskManagerTest, OpensMalformedRelationLengthAndRemovesDurably) {
    auto root = test_support::TestDirectory::create("disk-manager-lifecycle");
    ASSERT_TRUE(root);
    const auto path = root.value().join("relations");
    auto layout = RelationFileLayout::create(path);
    ASSERT_TRUE(layout);
    auto malformed = layout.value().create_segment(base::RelationId{23}, 0);
    ASSERT_TRUE(malformed);
    const std::array one_byte{std::byte{1}};
    ASSERT_EQ(::fseeko(malformed.value().get(), 0, SEEK_SET), 0);
    ASSERT_EQ(
        std::fwrite(one_byte.data(), 1, one_byte.size(), malformed.value().get()),
        one_byte.size()
    );
    ASSERT_EQ(std::fflush(malformed.value().get()), 0);

    auto manager = FileDiskManager::open(path);
    ASSERT_TRUE(manager);
    const auto count = manager.value()->page_count(base::RelationId{23});
    ASSERT_TRUE(count);
    EXPECT_EQ(count.value(), 0U);

    const auto allocated = manager.value()->allocate_page(base::RelationId{24}, PageKind::kHeap);
    ASSERT_TRUE(allocated);
    EXPECT_TRUE(manager.value()->remove_relation(base::RelationId{24}));
    EXPECT_FALSE(manager.value()->page_count(base::RelationId{24}));
    EXPECT_FALSE(manager.value()->remove_relation(base::RelationId{24}));
    EXPECT_TRUE(manager.value()->remove_relation(base::RelationId{24}, true));
    EXPECT_TRUE(manager.value()->remove_relation(base::kInvalidRelationId, true));
}

TEST(FileDiskManagerTest, SerializesConcurrentAllocationWithoutDuplicatePages) {
    auto root = test_support::TestDirectory::create("disk-manager-concurrent-allocation");
    ASSERT_TRUE(root);
    auto manager = FileDiskManager::open(root.value().join("relations"));
    ASSERT_TRUE(manager);
    constexpr std::size_t k_threads = 8;
    constexpr std::size_t k_pages_per_thread = 20;
    std::mutex results_mutex;
    std::vector<std::uint32_t> page_ids;
    page_ids.reserve(k_threads * k_pages_per_thread);
    std::vector<std::thread> threads;
    threads.reserve(k_threads);
    for (std::size_t thread = 0; thread < k_threads; ++thread) {
        threads.emplace_back([&] {
            for (std::size_t index = 0; index < k_pages_per_thread; ++index) {
                auto address =
                    manager.value()->allocate_page(base::RelationId{31}, PageKind::kHeap);
                ASSERT_TRUE(address);
                const std::lock_guard lock{results_mutex};
                page_ids.push_back(address.value().page_id.value());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    std::ranges::sort(page_ids);
    ASSERT_EQ(page_ids.size(), k_threads * k_pages_per_thread);
    for (std::size_t index = 0; index < page_ids.size(); ++index) {
        EXPECT_EQ(page_ids.at(index), index);
    }
    EXPECT_EQ(manager.value()->page_count(base::RelationId{31}).value(), page_ids.size());
}

TEST(FileDiskManagerTest, SerializesConcurrentAllocationAcrossSegmentRollover) {
    auto root = test_support::TestDirectory::create("disk-manager-segment-allocation");
    ASSERT_TRUE(root);
    auto manager = FileDiskManager::open(root.value().join("relations"));
    ASSERT_TRUE(manager);
    constexpr base::RelationId k_relation_id{32};
    const auto first = manager.value()->allocate_page(k_relation_id, PageKind::kHeap);
    ASSERT_TRUE(first);

    const auto last_page_id =
        base::PageId{static_cast<std::uint32_t>(detail::kRelationSegmentPages - 1U)};
    const PageAddress last_address{k_relation_id, last_page_id};
    auto last_page = Page::create(PageKind::kHeap, last_address);
    ASSERT_TRUE(last_page);
    ASSERT_TRUE(manager.value()->write_page(last_address, last_page.value()));
    ASSERT_EQ(manager.value()->page_count(k_relation_id).value(), detail::kRelationSegmentPages);

    constexpr std::size_t k_threads = 8;
    constexpr std::size_t k_pages_per_thread = 20;
    std::mutex results_mutex;
    std::vector<std::uint32_t> page_ids;
    page_ids.reserve(k_threads * k_pages_per_thread);
    std::vector<std::thread> threads;
    threads.reserve(k_threads);
    for (std::size_t thread = 0; thread < k_threads; ++thread) {
        threads.emplace_back([&] {
            for (std::size_t index = 0; index < k_pages_per_thread; ++index) {
                auto address = manager.value()->allocate_page(k_relation_id, PageKind::kHeap);
                ASSERT_TRUE(address);
                const std::lock_guard lock{results_mutex};
                page_ids.push_back(address.value().page_id.value());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    std::ranges::sort(page_ids);
    ASSERT_EQ(page_ids.size(), k_threads * k_pages_per_thread);
    for (std::size_t index = 0; index < page_ids.size(); ++index) {
        EXPECT_EQ(page_ids.at(index), detail::kRelationSegmentPages + index);
    }
    EXPECT_EQ(
        manager.value()->page_count(k_relation_id).value(),
        detail::kRelationSegmentPages + page_ids.size()
    );
}

TEST(FileDiskManagerTest, RoutesBoundaryPagesAcrossSegmentsSyncsAndReopens) {
    auto root = test_support::TestDirectory::create("disk-manager-segment-boundary");
    ASSERT_TRUE(root);
    const auto path = root.value().join("relations");
    constexpr base::RelationId k_relation_id{41};
    const auto last_page_id =
        base::PageId{static_cast<std::uint32_t>(detail::kRelationSegmentPages - 1U)};
    const auto first_next_segment_page_id =
        base::PageId{static_cast<std::uint32_t>(detail::kRelationSegmentPages)};
    const auto second_next_segment_page_id =
        base::PageId{static_cast<std::uint32_t>(detail::kRelationSegmentPages + 1U)};

    auto manager = FileDiskManager::open(path);
    ASSERT_TRUE(manager);
    const auto first = manager.value()->allocate_page(k_relation_id, PageKind::kHeap);
    ASSERT_TRUE(first);
    EXPECT_EQ(first.value().page_id, base::PageId{0});

    const auto write_marker = [&](const PageAddress address, const std::byte marker) {
        auto page = Page::create(PageKind::kHeap, address);
        ASSERT_TRUE(page);
        auto payload = page.value().mutable_view(100, 1);
        ASSERT_TRUE(payload);
        payload.value().front() = marker;
        ASSERT_TRUE(manager.value()->write_page(address, page.value()));
    };

    // This raw high-page write must materialize a sparse full first segment
    // before the second segment is created, just like an extension during
    // recovery. It therefore preserves the global PageId sequence.
    write_marker(PageAddress{k_relation_id, second_next_segment_page_id}, std::byte{0xC3});
    write_marker(PageAddress{k_relation_id, last_page_id}, std::byte{0xA1});
    write_marker(PageAddress{k_relation_id, first_next_segment_page_id}, std::byte{0xB2});

    const auto allocated = manager.value()->allocate_page(k_relation_id, PageKind::kBtree);
    ASSERT_TRUE(allocated);
    EXPECT_EQ(
        allocated.value().page_id,
        base::PageId{static_cast<std::uint32_t>(detail::kRelationSegmentPages + 2U)}
    );
    EXPECT_EQ(
        manager.value()->page_count(k_relation_id).value(),
        detail::kRelationSegmentPages + 3U
    );

    auto padded = manager.value()->read_page(PageAddress{k_relation_id, base::PageId{1}});
    ASSERT_TRUE(padded);
    EXPECT_TRUE(std::ranges::all_of(padded.value().bytes(), [](const std::byte value) {
        return value == std::byte{0};
    }));

    auto layout = RelationFileLayout::open(path);
    ASSERT_TRUE(layout);
    const auto first_segment = layout.value().segment_path(k_relation_id, 0).value();
    const auto second_segment = layout.value().segment_path(k_relation_id, 1).value();
    EXPECT_EQ(std::filesystem::file_size(first_segment), detail::kRelationSegmentBytes);
    EXPECT_EQ(std::filesystem::file_size(second_segment), 3U * kPageSize);
    EXPECT_TRUE(manager.value()->sync_relation(k_relation_id));
    EXPECT_TRUE(manager.value()->sync_all());

    manager = FileDiskManager::open(path);
    ASSERT_TRUE(manager);
    EXPECT_EQ(
        manager.value()->page_count(k_relation_id).value(),
        detail::kRelationSegmentPages + 3U
    );
    for (const auto& [page_id, marker] : std::array{
             std::pair{last_page_id, std::byte{0xA1}},
             std::pair{first_next_segment_page_id, std::byte{0xB2}},
             std::pair{second_next_segment_page_id, std::byte{0xC3}},
         }) {
        auto page = manager.value()->read_page(PageAddress{k_relation_id, page_id});
        ASSERT_TRUE(page);
        EXPECT_EQ(page.value().view(100, 1).value().front(), marker);
    }
}

TEST(FileDiskManagerTest, RemovesUnopenedSegmentsAfterRestart) {
    auto root = test_support::TestDirectory::create("disk-manager-segment-remove");
    ASSERT_TRUE(root);
    const auto path = root.value().join("relations");
    constexpr base::RelationId k_relation_id{43};
    auto layout = RelationFileLayout::create(path);
    ASSERT_TRUE(layout);
    const auto first_segment = layout.value().segment_path(k_relation_id, 0).value();
    const auto second_segment = layout.value().segment_path(k_relation_id, 1).value();
    {
        auto first = layout.value().create_segment(k_relation_id, 0);
        ASSERT_TRUE(first);
        auto second = layout.value().create_segment(k_relation_id, 1);
        ASSERT_TRUE(second);
    }

    auto manager = FileDiskManager::open(path);
    ASSERT_TRUE(manager);
    EXPECT_TRUE(manager.value()->remove_relation(k_relation_id));
    EXPECT_FALSE(std::filesystem::exists(first_segment));
    EXPECT_FALSE(std::filesystem::exists(second_segment));
    EXPECT_FALSE(manager.value()->page_count(k_relation_id));
}

TEST(DurableMetadataTest, PublishesValidatesAndReplacesVersionedGenerations) {
    auto root = test_support::TestDirectory::create("durable-metadata-roundtrip");
    ASSERT_TRUE(root);
    const auto path = root.value().join("control/state");
    constexpr std::uint32_t k_magic = 0x4154454DU;
    EXPECT_TRUE(publish_metadata(path, k_magic, 2, 7, bytes("old")));
    EXPECT_TRUE(publish_metadata(path, k_magic, 2, 8, bytes("new value")));

    const auto loaded = load_metadata(path, MetadataReadOptions{k_magic, 2, 1024});
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded.value().generation, 8U);
    EXPECT_TRUE(std::ranges::equal(base::Bytes{loaded.value().payload}, bytes("new value")));
    EXPECT_FALSE(load_metadata(path, MetadataReadOptions{k_magic + 1, 2, 1024}));
    const auto unsupported = load_metadata(path, MetadataReadOptions{k_magic, 3, 1024});
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_FALSE(load_metadata(path, MetadataReadOptions{k_magic, 2, 2}));
}

TEST(DurableMetadataTest, RejectsInvalidPublishingAndCorruptImages) {
    auto root = test_support::TestDirectory::create("durable-metadata-errors");
    ASSERT_TRUE(root);
    const auto path = root.value().join("state");
    constexpr std::uint32_t k_magic = 0x4154454DU;
    EXPECT_FALSE(publish_metadata(path, 0, 1, 0, {}));
    EXPECT_FALSE(publish_metadata(path, k_magic, 0, 0, {}));
    EXPECT_FALSE(load_metadata(path, MetadataReadOptions{k_magic, 1, 10}));
    ASSERT_TRUE(publish_metadata(path, k_magic, 1, 1, bytes("payload")));

    auto file =
        io::PosixFile::open(path, io::FileOpenOptions{.access = io::FileAccess::kReadWrite});
    ASSERT_TRUE(file);
    const std::array corrupt{std::byte{0x7F}};
    ASSERT_TRUE(file.value().write_all_at(kDurableMetadataHeaderSize, corrupt));
    const auto loaded = load_metadata(path, MetadataReadOptions{k_magic, 1, 1024});
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error().code(), base::ErrorCode::kCorruption);
}

}  // namespace
}  // namespace db::storage
