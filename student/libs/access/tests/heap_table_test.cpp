#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/heap_page.hpp"
#include "db/access/heap_table.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/free_space_map.hpp"
#include "db/storage/page.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/transaction/commit_log.hpp"

namespace db::access {
namespace {

class AcceptingDurability final : public storage::PageDurabilityPort {
public:
    auto flush_through(const base::Lsn target_lsn) -> base::Status override {
        if (target_lsn == base::kInvalidLsn) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "durability target must be valid",
                }
            );
        }
        return {};
    }
};

class RecordingHeapLog final : public HeapMutationLogPort {
public:
    auto log(const HeapWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.after_page == nullptr) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "heap mutation must provide an after page",
                }
            );
        }
        return base::Lsn{next_lsn_.fetch_add(8U)};
    }

private:
    std::atomic<std::uint64_t> next_lsn_{8};
};

auto make_pool(const test_support::TestDirectory& root, const std::size_t frames)
    -> std::unique_ptr<storage::BufferPool> {
    auto disk = storage::FileDiskManager::open(root.join("relations"));
    EXPECT_TRUE(disk) << disk.error().to_string();
    auto pool = storage::BufferPool::create(
        frames,
        std::move(disk).value(),
        nullptr,
        std::make_unique<AcceptingDurability>()
    );
    EXPECT_TRUE(pool) << pool.error().to_string();
    return std::move(pool).value();
}

template <typename Result>
void expect_unavailable(const Result& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inactive_heap_table(HeapTable& table) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(table.valid());
    EXPECT_EQ(table.relation_id(), base::kInvalidRelationId);
    EXPECT_EQ(table.fillfactor(), 0U);
    EXPECT_EQ(table.reserved_update_space(), 0U);
    EXPECT_EQ(table.page_count(), 0U);
    EXPECT_TRUE(table.page_ids().empty());

    expect_unavailable(table.is_physically_empty());
    expect_unavailable(table.ensure_initial_page());
    expect_unavailable(table.extend_page());
    expect_unavailable(table.insert(base::Xid{10}, 0U, {}));
    expect_unavailable(table.multi_insert(base::Xid{10}, 0U, {}));
    expect_unavailable(table.find_page_with_space(1U, HeapSpacePolicy::kOrdinaryInsert));
    expect_unavailable(table.refresh_free_space(base::PageId{0}));
    expect_unavailable(table.record_free_space(base::PageId{0}, 1U));
    expect_unavailable(table.persist_free_space_map());
    expect_unavailable(table.sync_relation());
    expect_unavailable(table.fetch(base::kInvalidRid, HeapFetchContext{}));
    expect_unavailable(table.update(HeapUpdateRequest{}, HeapUpdateContext{}));
    expect_unavailable(table.delete_tuple(HeapDeleteRequest{}, HeapMutationContext{}));
    const transaction::CommitLog commit_log;
    expect_unavailable(table.vacuum(commit_log, base::kInvalidXid));
    expect_unavailable(table.reclaim_index_cleaned_tids(DeadTidStore{}));
}

TEST(HeapTableTest, CreatesInitialPageAndReopensPersistentSpaceMap) {
    auto root = test_support::TestDirectory::create("heap-table-create-reopen");
    ASSERT_TRUE(root);
    auto pool = make_pool(root.value(), 1);
    RecordingHeapLog mutation_log;
    const auto fsm_path = root->join("fsm/relation-41.fsm");
    const auto relation_id = base::RelationId{41};

    {
        HeapSpaceRegistry registry;
        auto fsm = storage::FreeSpaceMap::create(fsm_path);
        ASSERT_TRUE(fsm);
        ASSERT_TRUE(registry.install(relation_id, std::move(fsm).value()));
        auto table = HeapTable::create(relation_id, *pool, registry, mutation_log);
        ASSERT_TRUE(table) << table.error().to_string();
        EXPECT_EQ(table->relation_id(), relation_id);
        EXPECT_EQ(table->fillfactor(), kDefaultHeapFillfactor);
        EXPECT_EQ(table->reserved_update_space(), 819U);
        EXPECT_EQ(table->page_count(), 1U);
        EXPECT_EQ(table->page_ids(), std::vector<base::PageId>{base::PageId{0}});

        auto read = pool->read_page({.relation_id = relation_id, .page_id = base::PageId{0}});
        ASSERT_TRUE(read);
        EXPECT_EQ(read->page()->header()->kind, storage::PageKind::kHeap);
        EXPECT_TRUE(table->persist_free_space_map());
        EXPECT_TRUE(table->sync_relation());
    }

    auto reopened_fsm = storage::FreeSpaceMap::open(fsm_path);
    ASSERT_TRUE(reopened_fsm);
    HeapSpaceRegistry reopened_registry;
    ASSERT_TRUE(reopened_registry.install(relation_id, std::move(reopened_fsm).value()));
    auto reopened = HeapTable::open(relation_id, *pool, reopened_registry, mutation_log);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->page_count(), 1U);
    EXPECT_EQ(reopened->ensure_initial_page().value(), base::PageId{0});

    auto duplicate = HeapTable::create(relation_id, *pool, reopened_registry, mutation_log);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kAlreadyExists);
}

TEST(HeapTableTest, MoveTransfersTheRelationAndLeavesTheSourceInert) {
    auto root = test_support::TestDirectory::create("heap-table-move-lifecycle");
    ASSERT_TRUE(root);
    auto pool = make_pool(root.value(), 2U);
    RecordingHeapLog mutation_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{72};
    auto source = HeapTable::create(relation_id, *pool, registry, mutation_log).value();

    HeapTable moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.relation_id(), relation_id);
    EXPECT_EQ(moved.page_count(), 1U);
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_heap_table(source);

    auto assigned = HeapTable::create(base::RelationId{73}, *pool, registry, mutation_log).value();
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    EXPECT_EQ(assigned.relation_id(), relation_id);
    EXPECT_TRUE(assigned.is_physically_empty().value());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_heap_table(moved);
}

TEST(HeapTableTest, FillfactorAndFsmChooseOrdinaryVersusHotTargets) {
    auto root = test_support::TestDirectory::create("heap-table-space-policy");
    ASSERT_TRUE(root);
    auto pool = make_pool(root.value(), 2);
    RecordingHeapLog mutation_log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{51}, *pool, registry, mutation_log);
    ASSERT_TRUE(table);
    EXPECT_EQ(table->extend_page().value(), base::PageId{1});

    ASSERT_TRUE(table->record_free_space(base::PageId{0}, 0));
    auto ordinary = table->find_page_with_space(100, HeapSpacePolicy::kOrdinaryInsert);
    ASSERT_TRUE(ordinary);
    ASSERT_TRUE(ordinary->has_value());
    EXPECT_EQ(ordinary->value(), base::PageId{1});

    constexpr std::uint16_t k_large_tuple = 3'300;
    EXPECT_FALSE(
        table->find_page_with_space(k_large_tuple, HeapSpacePolicy::kOrdinaryInsert)->has_value()
    );
    auto hot = table->find_page_with_space(k_large_tuple, HeapSpacePolicy::kHotUpdate);
    ASSERT_TRUE(hot);
    EXPECT_TRUE(hot->has_value());

    EXPECT_EQ(
        table->record_free_space(base::PageId{2}, 100).error().code(),
        base::ErrorCode::kOutOfRange
    );
    EXPECT_EQ(
        table->refresh_free_space(base::PageId{2}).error().code(),
        base::ErrorCode::kOutOfRange
    );
}

TEST(HeapTableTest, EmptyOpenCanInitializeAndInvalidDescriptorsAreRejected) {
    auto root = test_support::TestDirectory::create("heap-table-empty-open");
    ASSERT_TRUE(root);
    auto pool = make_pool(root.value(), 1);
    RecordingHeapLog mutation_log;
    HeapSpaceRegistry registry;

    auto empty = HeapTable::open(base::RelationId{61}, *pool, registry, mutation_log);
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->page_count(), 0U);
    EXPECT_EQ(empty->ensure_initial_page().value(), base::PageId{0});
    EXPECT_EQ(empty->page_count(), 1U);

    EXPECT_EQ(
        HeapTable::open(base::kInvalidRelationId, *pool, registry, mutation_log).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        HeapTable::open(base::RelationId{62}, *pool, registry, mutation_log, 9).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        HeapTable::open(base::RelationId{62}, *pool, registry, mutation_log, 101).error().code(),
        base::ErrorCode::kInvalidArgument
    );
}

TEST(HeapTableTest, PhysicalEmptinessTreatsTheInitialPageAsEmpty) {
    auto root = test_support::TestDirectory::create("heap-table-physical-empty");
    ASSERT_TRUE(root);
    auto pool = make_pool(root.value(), 1);
    RecordingHeapLog mutation_log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{63}, *pool, registry, mutation_log);
    ASSERT_TRUE(table);

    auto initially_empty = table->is_physically_empty();
    ASSERT_TRUE(initially_empty) << initially_empty.error().to_string();
    EXPECT_TRUE(initially_empty.value());

    ASSERT_TRUE(table->insert(base::Xid{10}, 0, std::vector<std::byte>{std::byte{1}}));
    auto after_insert = table->is_physically_empty();
    ASSERT_TRUE(after_insert) << after_insert.error().to_string();
    EXPECT_FALSE(after_insert.value());
}

TEST(HeapTableTest, OpenHandlesShareExtensionAndFreeSpaceState) {
    auto root = test_support::TestDirectory::create("heap-table-shared-space");
    ASSERT_TRUE(root);
    auto pool = make_pool(root.value(), 2);
    RecordingHeapLog mutation_log;
    const auto relation_id = base::RelationId{65};
    HeapSpaceRegistry registry;
    ASSERT_TRUE(registry.install(relation_id, storage::FreeSpaceMap{}));
    EXPECT_TRUE(registry.contains(relation_id));
    EXPECT_EQ(registry.size(), 1U);
    EXPECT_EQ(
        registry.install(relation_id, storage::FreeSpaceMap{}).error().code(),
        base::ErrorCode::kAlreadyExists
    );

    {
        auto first = HeapTable::create(relation_id, *pool, registry, mutation_log).value();
        auto second = HeapTable::open(relation_id, *pool, registry, mutation_log).value();
        EXPECT_EQ(second.extend_page().value(), base::PageId{1});
        EXPECT_EQ(first.page_count(), 2U);
        EXPECT_EQ(first.page_ids().back(), base::PageId{1});

        ASSERT_TRUE(first.record_free_space(base::PageId{0}, 0));
        auto candidate = second.find_page_with_space(100, HeapSpacePolicy::kOrdinaryInsert);
        ASSERT_TRUE(candidate);
        ASSERT_TRUE(candidate->has_value());
        EXPECT_EQ(candidate->value(), base::PageId{1});
    }

    EXPECT_TRUE(registry.remove(relation_id));
    EXPECT_FALSE(registry.contains(relation_id));
    EXPECT_EQ(registry.remove(relation_id).error().code(), base::ErrorCode::kNotFound);
}

TEST(HeapTableTest, OpenAdvancesTheSharedPageHorizonAfterStorageExtension) {
    auto root = test_support::TestDirectory::create("heap-table-open-extension-race");
    ASSERT_TRUE(root);
    auto pool = make_pool(root.value(), 2U);
    RecordingHeapLog mutation_log;
    HeapSpaceRegistry registry;
    constexpr auto relation_id = base::RelationId{68};
    auto table = HeapTable::create(relation_id, *pool, registry, mutation_log).value();

    // This models the interval in HeapTable::extend_page after the storage
    // allocation has become visible but before the access-layer horizon is
    // advanced under its relation mutex.
    auto allocated = pool->allocate_page(relation_id, storage::PageKind::kHeap);
    ASSERT_TRUE(allocated) << allocated.error().to_string();

    auto reopened = HeapTable::open(relation_id, *pool, registry, mutation_log);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->page_count(), 2U);
    EXPECT_EQ(table.page_count(), 2U);
}

TEST(HeapTableTest, ColdRegistryReopenRecoversTheRelationTailAsAnInsertCandidate) {
    auto root = test_support::TestDirectory::create("heap-table-cold-registry");
    ASSERT_TRUE(root);
    auto pool = make_pool(root.value(), 2);
    RecordingHeapLog mutation_log;
    constexpr auto relation_id = base::RelationId{66};
    {
        HeapSpaceRegistry initial_registry;
        auto table = HeapTable::create(relation_id, *pool, initial_registry, mutation_log).value();
        ASSERT_TRUE(table.insert(base::Xid{10}, 0, std::vector<std::byte>{std::byte{1}}));
    }

    HeapSpaceRegistry cold_registry;
    auto reopened = HeapTable::open(relation_id, *pool, cold_registry, mutation_log).value();
    const auto second = reopened.insert(base::Xid{11}, 0, std::vector<std::byte>{std::byte{2}});

    ASSERT_TRUE(second);
    EXPECT_EQ(second->page_id(), base::PageId{0});
    EXPECT_EQ(reopened.page_count(), 1U);
}

TEST(HeapTableTest, InsertRepairsAStaleFsmHintBeforeExtendingTheRelation) {
    auto root = test_support::TestDirectory::create("heap-table-stale-fsm");
    ASSERT_TRUE(root);
    auto pool = make_pool(root.value(), 3);
    RecordingHeapLog mutation_log;
    HeapSpaceRegistry registry;
    auto table =
        HeapTable::create(base::RelationId{67}, *pool, registry, mutation_log, 100).value();
    const std::vector<std::byte> payload(1'000U, std::byte{7});
    for (std::uint32_t xid = 10; xid < 14; ++xid) {
        ASSERT_TRUE(table.insert(base::Xid{xid}, 0, payload));
    }
    ASSERT_EQ(table.page_count(), 2U);
    ASSERT_TRUE(table.record_free_space(base::PageId{0}, storage::kMaximumPageFreeSpace));

    const auto inserted = table.insert(base::Xid{14}, 0, payload);

    ASSERT_TRUE(inserted);
    EXPECT_EQ(inserted->page_id(), base::PageId{1});
    EXPECT_EQ(table.page_count(), 2U);
    auto stale_candidate = table.find_page_with_space(
        static_cast<std::uint16_t>(kTupleHeaderSize + payload.size()),
        HeapSpacePolicy::kOrdinaryInsert
    );
    ASSERT_TRUE(stale_candidate);
    EXPECT_NE(stale_candidate.value(), std::optional<base::PageId>{base::PageId{0}});
}

TEST(HeapTableTest, ConcurrentExtensionsRemainContiguous) {
    auto root = test_support::TestDirectory::create("heap-table-concurrent-extension");
    ASSERT_TRUE(root);
    auto pool = make_pool(root.value(), 4);
    RecordingHeapLog mutation_log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{71}, *pool, registry, mutation_log).value();

    constexpr std::size_t k_workers = 8;
    std::mutex result_mutex;
    std::vector<base::PageId> pages;
    std::vector<base::Error> errors;
    std::vector<std::thread> workers;
    workers.reserve(k_workers);
    for (std::size_t worker = 0; worker < k_workers; ++worker) {
        workers.emplace_back([&] {
            auto page = table.extend_page();
            const std::lock_guard lock{result_mutex};
            if (page) {
                pages.push_back(page.value());
            } else {
                errors.push_back(std::move(page).error());
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }

    ASSERT_TRUE(errors.empty());
    ASSERT_EQ(pages.size(), k_workers);
    std::sort(pages.begin(), pages.end());
    for (std::size_t index = 0; index < pages.size(); ++index) {
        EXPECT_EQ(pages[index], base::PageId{static_cast<std::uint32_t>(index + 1U)});
    }
    EXPECT_EQ(table.page_count(), k_workers + 1U);
    EXPECT_EQ(pool->relation_page_count(base::RelationId{71}).value(), k_workers + 1U);
}

}  // namespace
}  // namespace db::access
