#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_maintenance.hpp"
#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"
#include "db/access/heap_maintenance.hpp"
#include "db/base/error.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "btree_test_log.hpp"

namespace db::access {
namespace {

struct MaintainedTree final {
    std::unique_ptr<storage::BufferPool> pool;
    BtreeRelation relation;
};

auto make_tree(const test_support::TestDirectory& directory, const base::RelationId relation_id)
    -> base::Result<MaintainedTree> {
    auto disk = storage::FileDiskManager::open(directory.join("relations"));
    if (!disk) {
        return base::fail(std::move(disk).error());
    }
    auto pool = storage::BufferPool::create(
        16U,
        std::move(disk).value(),
        nullptr,
        test::btree_durability()
    );
    if (!pool) {
        return base::fail(std::move(pool).error());
    }
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    if (!layout) {
        return base::fail(std::move(layout).error());
    }
    auto relation =
        BtreeRelation::create(relation_id, *pool.value(), layout.value(), false, test::btree_log());
    if (!relation) {
        return base::fail(std::move(relation).error());
    }
    return MaintainedTree{
        .pool = std::move(pool).value(),
        .relation = std::move(relation).value(),
    };
}

auto insert_integer(
    const BtreeRelation& relation,
    storage::BufferPool& pool,
    const std::int64_t key,
    const base::Rid rid
) -> base::Status {
    const std::array values{types::Value::integer(key)};
    return relation.insert(pool, std::span<const types::Value>{values}, rid);
}

auto collect_candidates(
    const BtreeRelation& relation,
    storage::BufferPool& pool,
    const std::span<const types::Value> values
) -> base::Result<std::vector<BtreeScanCandidate>> {
    auto scan = BtreeScan::for_eq(relation, values, {});
    if (!scan) {
        return base::fail(std::move(scan).error());
    }
    std::vector<BtreeScanCandidate> candidates;
    for (;;) {
        auto candidate = scan->next_candidate(pool);
        if (!candidate) {
            return base::fail(std::move(candidate).error());
        }
        if (!candidate->has_value()) {
            return candidates;
        }
        candidates.push_back(candidate->value());
    }
}

auto collect_all(const BtreeRelation& relation, storage::BufferPool& pool)
    -> base::Result<std::vector<base::Rid>> {
    auto scan = BtreeScan::for_full(relation, {});
    if (!scan) {
        return base::fail(std::move(scan).error());
    }
    std::vector<base::Rid> rows;
    for (;;) {
        auto next = scan->next(pool);
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!next->has_value()) {
            return rows;
        }
        rows.push_back(next->value());
    }
}

template <typename Result>
void expect_unavailable(const Result& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inactive_cleanup_batch(
    BtreeCleanupBatch& batch,
    const BtreeRelation& relation,
    storage::BufferPool& pool,
    const std::span<const types::Value> key
) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(batch.valid());
    EXPECT_TRUE(batch.empty());
    expect_unavailable(batch.reset_equal(relation, key));
    expect_unavailable(batch.push_dead(BtreeScanCandidate{}));
    expect_unavailable(batch.flush(pool));
}

TEST(BtreeCleanupBatchTest, MoveTransfersTheBatchAndLeavesTheSourceInert) {
    auto directory = test_support::TestDirectory::create("btree-cleanup-move-lifecycle");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_tree(directory.value(), base::RelationId{659});
    ASSERT_TRUE(tree) << tree.error().to_string();
    const std::array key{types::Value::integer(7)};
    auto source = BtreeCleanupBatch::for_equal(tree->relation, key).value();

    BtreeCleanupBatch moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    EXPECT_TRUE(moved.empty());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_cleanup_batch(source, tree->relation, *tree->pool, key);

    auto assigned = BtreeCleanupBatch::for_equal(tree->relation, key).value();
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    EXPECT_TRUE(assigned.empty());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_cleanup_batch(moved, tree->relation, *tree->pool, key);
}

TEST(BtreeCleanupBatchTest, RepairsOneEqualRunAcrossMultipleLeafPages) {
    auto directory = test_support::TestDirectory::create("btree-cleanup-equal-run");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_tree(directory.value(), base::RelationId{660});
    ASSERT_TRUE(tree) << tree.error().to_string();
    std::vector<BtreeBulkBuildEntry> entries;
    entries.reserve(500U);
    const std::array key{types::Value::integer(7)};
    for (std::uint32_t index = 0U; index < 500U; ++index) {
        auto entry = tree->relation.make_bulk_build_entry(
            key,
            base::Rid{base::PageId{index + 100U}, base::SlotId{1}}
        );
        ASSERT_TRUE(entry) << entry.error().to_string();
        entries.push_back(std::move(entry).value());
    }
    ASSERT_TRUE(tree->relation.bulk_build(*tree->pool, entries));

    auto candidates = collect_candidates(tree->relation, *tree->pool, key);
    ASSERT_TRUE(candidates) << candidates.error().to_string();
    ASSERT_EQ(candidates->size(), 500U);
    std::set<base::PageId> leaf_pages;
    for (const auto candidate : candidates.value()) {
        leaf_pages.insert(candidate.leaf_page_id);
    }
    ASSERT_GT(leaf_pages.size(), 1U);

    const auto survivor = candidates->back();
    auto cleanup = BtreeCleanupBatch::for_equal(tree->relation, key);
    ASSERT_TRUE(cleanup) << cleanup.error().to_string();
    for (std::size_t index = 0U; index + 1U < candidates->size(); ++index) {
        ASSERT_TRUE(cleanup->push_dead(candidates->at(index)));
    }
    auto stats = cleanup->flush(*tree->pool);
    ASSERT_TRUE(stats) << stats.error().to_string();
    EXPECT_GT(stats->pages_changed, 1U);
    EXPECT_EQ(stats->pages_busy, 0U);
    EXPECT_EQ(stats->rids_removed, 499U);

    auto remaining = collect_candidates(tree->relation, *tree->pool, key);
    ASSERT_TRUE(remaining) << remaining.error().to_string();
    EXPECT_EQ(remaining.value(), (std::vector{survivor}));
}

TEST(BtreeCleanupBatchTest, SkipsABusyLeafWithoutBlockingOrRemovingItsCandidate) {
    auto directory = test_support::TestDirectory::create("btree-cleanup-busy");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_tree(directory.value(), base::RelationId{661});
    ASSERT_TRUE(tree) << tree.error().to_string();
    const auto rid = base::Rid{base::PageId{10}, base::SlotId{1}};
    ASSERT_TRUE(insert_integer(tree->relation, *tree->pool, 7, rid));

    const std::array key{types::Value::integer(7)};
    auto candidates = collect_candidates(tree->relation, *tree->pool, key);
    ASSERT_TRUE(candidates) << candidates.error().to_string();
    ASSERT_EQ(candidates->size(), 1U);
    auto cleanup = BtreeCleanupBatch::for_equal(tree->relation, key);
    ASSERT_TRUE(cleanup) << cleanup.error().to_string();
    auto wrong_relation = candidates->front();
    wrong_relation.relation_id = base::RelationId{999};
    auto rejected = cleanup->push_dead(wrong_relation);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kInvalidArgument);
    ASSERT_TRUE(cleanup->push_dead(candidates->front()));

    const auto address = storage::PageAddress{
        tree->relation.relation_id(),
        candidates->front().leaf_page_id,
    };
    auto busy = tree->pool->read_page(address);
    ASSERT_TRUE(busy) << busy.error().to_string();
    auto stats = cleanup->flush(*tree->pool);
    ASSERT_TRUE(stats) << stats.error().to_string();
    EXPECT_EQ(stats->pages_changed, 0U);
    EXPECT_EQ(stats->pages_busy, 1U);
    EXPECT_EQ(stats->rids_removed, 0U);

    auto remaining = collect_candidates(tree->relation, *tree->pool, key);
    ASSERT_TRUE(remaining) << remaining.error().to_string();
    EXPECT_EQ(remaining.value(), candidates.value());
}

TEST(BtreeVacuumTest, BulkDeleteCompactsLeavesWithoutUnlinkingPages) {
    auto directory = test_support::TestDirectory::create("btree-vacuum");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_tree(directory.value(), base::RelationId{662});
    ASSERT_TRUE(tree) << tree.error().to_string();
    DeadTidStore dead;
    std::vector<base::Rid> survivors;
    std::vector<BtreeBulkBuildEntry> entries;
    entries.reserve(600U);
    for (std::uint32_t index = 0U; index < 600U; ++index) {
        const auto rid = base::Rid{base::PageId{index + 1U}, base::SlotId{1}};
        const std::array key{types::Value::integer(index)};
        auto entry = tree->relation.make_bulk_build_entry(key, rid);
        ASSERT_TRUE(entry) << entry.error().to_string();
        entries.push_back(std::move(entry).value());
        if (index < 450U) {
            ASSERT_TRUE(dead.insert(rid));
        } else {
            survivors.push_back(rid);
        }
    }
    ASSERT_TRUE(tree->relation.bulk_build(*tree->pool, entries));
    const auto page_count_before =
        tree->pool->relation_page_count(tree->relation.relation_id()).value();

    auto vacuumed = tree->relation.bulk_delete(*tree->pool, dead);
    ASSERT_TRUE(vacuumed) << vacuumed.error().to_string();
    EXPECT_GT(vacuumed->pages_scanned, 1U);
    EXPECT_GT(vacuumed->pages_compacted, 0U);
    EXPECT_EQ(vacuumed->rids_removed, 450U);
    EXPECT_EQ(
        tree->pool->relation_page_count(tree->relation.relation_id()).value(),
        page_count_before
    );

    auto remaining = collect_all(tree->relation, *tree->pool);
    ASSERT_TRUE(remaining) << remaining.error().to_string();
    EXPECT_EQ(remaining.value(), survivors);
    auto compacted = tree->relation.vacuum(*tree->pool);
    ASSERT_TRUE(compacted) << compacted.error().to_string();
    EXPECT_EQ(compacted->pages_scanned, vacuumed->pages_scanned);
    EXPECT_EQ(compacted->pages_compacted, 0U);
    EXPECT_EQ(compacted->rids_removed, 0U);
}

}  // namespace
}  // namespace db::access
