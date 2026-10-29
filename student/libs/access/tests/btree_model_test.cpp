#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_maintenance.hpp"
#include "db/access/btree_page.hpp"
#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"
#include "db/access/dead_tid_store.hpp"
#include "db/base/error.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/test_support/deterministic_random.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "btree_test_log.hpp"

namespace db::access {
namespace {

constexpr std::size_t kBtreeModelUsableBytes =
    storage::kPageSize - storage::kPageHeaderSize - kBtreePageOpaqueSize;

auto make_pool(const test_support::TestDirectory& directory, const std::size_t frames = 32U)
    -> base::Result<std::unique_ptr<storage::BufferPool>> {
    auto disk = storage::FileDiskManager::open(directory.join("relations"));
    if (!disk) {
        return base::fail(std::move(disk).error());
    }
    return storage::BufferPool::create(
        frames,
        std::move(disk).value(),
        nullptr,
        test::btree_durability()
    );
}

constexpr auto model_rid(const std::int64_t key) noexcept -> base::Rid {
    return base::Rid{
        base::PageId{static_cast<std::uint32_t>(key + 10'000)},
        base::SlotId{1},
    };
}

auto insert_integer(
    const BtreeRelation& relation,
    storage::BufferPool& pool,
    const std::int64_t key,
    const base::Rid rid
) -> base::Status {
    const std::array values{types::Value::integer(key)};
    return relation.insert(pool, values, rid);
}

auto collect_forward(BtreeScan& scan, storage::BufferPool& pool)
    -> base::Result<std::vector<base::Rid>> {
    std::vector<base::Rid> result;
    for (;;) {
        auto next = scan.next(pool);
        if (!next) {
            return base::fail(std::move(next).error());
        }
        if (!next->has_value()) {
            return result;
        }
        result.push_back(next->value());
    }
}

auto leftmost_leaf(const BtreeRelation& relation, storage::BufferPool& pool)
    -> base::Result<base::PageId> {
    auto root = relation.root(pool);
    if (!root) {
        return base::fail(std::move(root).error());
    }
    if (!root->has_value()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kNotFound,
                "B-tree model relation is rootless",
            }
        );
    }
    auto page_id = root->value().page_id;
    for (;;) {
        const storage::PageAddress address{relation.relation_id(), page_id};
        auto read = pool.read_page(address);
        if (!read) {
            return base::fail(std::move(read).error());
        }
        auto page = BtreePage::open(*read->page(), address, relation.key_layout());
        if (!page) {
            return base::fail(std::move(page).error());
        }
        if (page->type() == BtreePageType::kLeaf) {
            return page_id;
        }
        auto first = page->tuple(0U);
        if (!first || !first->child_page_id.has_value()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kCorruption,
                    "B-tree model internal page has no first child",
                }
            );
        }
        page_id = first->child_page_id.value();
    }
}

auto average_non_rightmost_leaf_utilization(
    const BtreeRelation& relation,
    storage::BufferPool& pool
) -> base::Result<double> {
    auto first = leftmost_leaf(relation, pool);
    if (!first) {
        return base::fail(std::move(first).error());
    }
    auto page_id = first.value();
    double utilization_sum = 0.0;
    std::size_t non_rightmost = 0U;
    for (;;) {
        const storage::PageAddress address{relation.relation_id(), page_id};
        auto read = pool.read_page(address);
        if (!read) {
            return base::fail(std::move(read).error());
        }
        auto page = BtreePage::open(*read->page(), address, relation.key_layout());
        if (!page) {
            return base::fail(std::move(page).error());
        }
        auto links = page->links();
        if (!links) {
            return base::fail(std::move(links).error());
        }
        if (!links->next.has_value()) {
            if (non_rightmost == 0U) {
                return base::fail(
                    base::Error{
                        base::ErrorCode::kInvariantViolation,
                        "B-tree model requires multiple leaves",
                    }
                );
            }
            return utilization_sum / static_cast<double>(non_rightmost);
        }
        utilization_sum +=
            1.0
            - static_cast<double>(page->free_space()) / static_cast<double>(kBtreeModelUsableBytes);
        ++non_rightmost;
        page_id = links->next.value();
    }
}

auto scan_integer_range(
    const BtreeRelation& relation,
    storage::BufferPool& pool,
    const std::int64_t low,
    const std::int64_t high
) -> base::Result<std::vector<base::Rid>> {
    const std::array low_value{types::Value::integer(low)};
    const std::array high_value{types::Value::integer(high)};
    auto scan = BtreeScan::for_range(
        relation,
        BtreeScanRange{
            .low = low_value,
            .high = high_value,
            .low_exclusive = false,
            .high_exclusive = false,
        },
        {}
    );
    if (!scan) {
        return base::fail(std::move(scan).error());
    }
    return collect_forward(scan.value(), pool);
}

TEST(BtreeModelTest, SequentialPointLookupAndDurableReopenKeepADenseDeepTree) {
    auto directory = test_support::TestDirectory::create("btree-model-sequential");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool = make_pool(directory.value());
    ASSERT_TRUE(pool) << pool.error().to_string();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    constexpr auto relation_id = base::RelationId{730};
    auto relation =
        BtreeRelation::create(relation_id, **pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    constexpr std::int64_t entry_count = 1'500;
    for (std::int64_t key = 0; key < entry_count; ++key) {
        ASSERT_TRUE(insert_integer(relation.value(), **pool, key, model_rid(key)));
    }
    auto root = relation->root(**pool);
    ASSERT_TRUE(root) << root.error().to_string();
    ASSERT_TRUE(root->has_value());
    EXPECT_GE(root->value().level, 1U);

    auto full = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(full) << full.error().to_string();
    auto all = collect_forward(full.value(), **pool);
    ASSERT_TRUE(all) << all.error().to_string();
    ASSERT_EQ(all->size(), static_cast<std::size_t>(entry_count));
    ASSERT_GE(full->leaf_pages_visited(), 10U);

    const std::array point{types::Value::integer(entry_count / 2)};
    auto lookup = BtreeScan::for_eq(relation.value(), point, {});
    ASSERT_TRUE(lookup) << lookup.error().to_string();
    auto found = collect_forward(lookup.value(), **pool);
    ASSERT_TRUE(found) << found.error().to_string();
    EXPECT_EQ(found.value(), (std::vector{model_rid(entry_count / 2)}));
    EXPECT_LE(lookup->leaf_pages_visited(), 2U);
    EXPECT_LT(lookup->leaf_items_examined(), all->size() / 20U);

    auto utilization = average_non_rightmost_leaf_utilization(relation.value(), **pool);
    ASSERT_TRUE(utilization) << utilization.error().to_string();
    EXPECT_GE(utilization.value(), 0.85);

    ASSERT_TRUE((*pool)->flush_all());
    ASSERT_TRUE((*pool)->sync_relation(relation_id));
    ASSERT_TRUE((*pool)->shutdown());
    auto reopened_pool = make_pool(directory.value());
    ASSERT_TRUE(reopened_pool) << reopened_pool.error().to_string();
    auto reopened =
        BtreeRelation::open(relation_id, **reopened_pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    auto reopened_scan = BtreeScan::for_full(reopened.value(), {});
    ASSERT_TRUE(reopened_scan) << reopened_scan.error().to_string();
    auto reopened_rows = collect_forward(reopened_scan.value(), **reopened_pool);
    ASSERT_TRUE(reopened_rows) << reopened_rows.error().to_string();
    EXPECT_EQ(reopened_rows.value(), all.value());
}

TEST(BtreeModelTest, FixedSeedInsertDeleteAndRangeScansMatchOrderedModel) {
    auto directory = test_support::TestDirectory::create("btree-model-model");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool = make_pool(directory.value());
    ASSERT_TRUE(pool) << pool.error().to_string();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    constexpr auto relation_id = base::RelationId{731};
    auto relation =
        BtreeRelation::create(relation_id, **pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    test_support::DeterministicRandom random{0x7ECC'5EED'B7EE'2026ULL};
    std::map<std::int64_t, base::Rid> model;
    constexpr std::size_t step_count = 1'000U;
    for (std::size_t step = 0U; step < step_count; ++step) {
        const auto key = static_cast<std::int64_t>(random.uniform(401U).value()) - 200;
        const auto rid = model_rid(key);
        if ((random.next_u64() & 1U) == 0U) {
            if (!model.contains(key)) {
                ASSERT_TRUE(insert_integer(relation.value(), **pool, key, rid));
                model.emplace(key, rid);
            }
        } else if (model.contains(key)) {
            const std::array values{types::Value::integer(key)};
            auto scan = BtreeScan::for_eq(relation.value(), values, {});
            ASSERT_TRUE(scan) << scan.error().to_string();
            auto candidate = scan->next_candidate(**pool);
            ASSERT_TRUE(candidate) << candidate.error().to_string();
            ASSERT_TRUE(candidate->has_value());
            auto cleanup = BtreeCleanupBatch::for_equal(relation.value(), values);
            ASSERT_TRUE(cleanup) << cleanup.error().to_string();
            ASSERT_TRUE(cleanup->push_dead(candidate->value()));
            auto removed = cleanup->flush(**pool);
            ASSERT_TRUE(removed) << removed.error().to_string();
            ASSERT_EQ(removed->rids_removed, 1U);
            model.erase(key);
        }

        if (step % 25U == 0U) {
            const auto first = static_cast<std::int64_t>(random.uniform(401U).value()) - 200;
            const auto second = static_cast<std::int64_t>(random.uniform(401U).value()) - 200;
            const auto [low, high] = std::minmax(first, second);
            auto actual = scan_integer_range(relation.value(), **pool, low, high);
            ASSERT_TRUE(actual) << actual.error().to_string();
            std::vector<base::Rid> expected;
            for (auto entry = model.lower_bound(low); entry != model.end() && entry->first <= high;
                 ++entry) {
                expected.push_back(entry->second);
            }
            EXPECT_EQ(actual.value(), expected) << "model mismatch at step " << step;
        }
    }

    DeadTidStore absent;
    ASSERT_TRUE(absent.insert(base::Rid{base::PageId{999'999}, base::SlotId{7}}));
    auto no_op = relation->bulk_delete(**pool, absent);
    ASSERT_TRUE(no_op) << no_op.error().to_string();
    EXPECT_EQ(no_op->rids_removed, 0U);
    auto vacuumed = relation->vacuum(**pool);
    ASSERT_TRUE(vacuumed) << vacuumed.error().to_string();
    EXPECT_EQ(vacuumed->rids_removed, 0U);

    auto full = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(full) << full.error().to_string();
    auto actual = collect_forward(full.value(), **pool);
    ASSERT_TRUE(actual) << actual.error().to_string();
    std::vector<base::Rid> expected;
    expected.reserve(model.size());
    for (const auto& [key, rid] : model) {
        static_cast<void>(key);
        expected.push_back(rid);
    }
    EXPECT_EQ(actual.value(), expected);
}

}  // namespace
}  // namespace db::access
