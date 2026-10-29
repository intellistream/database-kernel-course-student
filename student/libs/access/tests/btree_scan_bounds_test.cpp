#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_key.hpp"
#include "db/access/btree_relation.hpp"
#include "db/access/btree_scan.hpp"
#include "db/base/identifier.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "btree_test_log.hpp"

namespace db::access {
namespace {

struct CompositeTree final {
    std::unique_ptr<storage::BufferPool> pool;
    BtreeRelation relation;
};

auto make_composite_tree(
    const test_support::TestDirectory& directory,
    const base::RelationId relation_id
) -> base::Result<CompositeTree> {
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
    auto layout = BtreeKeyLayout::create({types::kInt64Type, types::kInt64Type, types::kInt64Type});
    if (!layout) {
        return base::fail(std::move(layout).error());
    }
    auto relation =
        BtreeRelation::create(relation_id, *pool.value(), layout.value(), false, test::btree_log());
    if (!relation) {
        return base::fail(std::move(relation).error());
    }
    return CompositeTree{
        .pool = std::move(pool).value(),
        .relation = std::move(relation).value(),
    };
}

auto populate(CompositeTree& tree) -> base::Status {
    for (std::int64_t first = 0; first < 12; ++first) {
        for (std::int64_t second = 0; second < 4; ++second) {
            for (std::int64_t third = 0; third < 10; ++third) {
                const std::array values{
                    types::Value::integer(first),
                    types::Value::integer(second),
                    types::Value::integer(third),
                };
                auto inserted = tree.relation.insert(
                    *tree.pool,
                    values,
                    base::Rid{
                        base::PageId{static_cast<std::uint32_t>(100 + first)},
                        base::SlotId{static_cast<std::uint16_t>(1 + second * 10 + third)},
                    }
                );
                if (!inserted) {
                    return inserted;
                }
            }
        }
    }
    return {};
}

auto make_character_tree(
    const test_support::TestDirectory& directory,
    const base::RelationId relation_id
) -> base::Result<CompositeTree> {
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
    auto layout =
        BtreeKeyLayout::create({types::DataType{types::kCharTypeId, types::TypeModifier{2}}});
    if (!layout) {
        return base::fail(std::move(layout).error());
    }
    auto relation =
        BtreeRelation::create(relation_id, *pool.value(), layout.value(), false, test::btree_log());
    if (!relation) {
        return base::fail(std::move(relation).error());
    }
    return CompositeTree{
        .pool = std::move(pool).value(),
        .relation = std::move(relation).value(),
    };
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

auto collect_backward(BtreeScan& scan, storage::BufferPool& pool)
    -> base::Result<std::vector<base::Rid>> {
    std::vector<base::Rid> result;
    for (;;) {
        auto previous = scan.previous(pool);
        if (!previous) {
            return base::fail(std::move(previous).error());
        }
        if (!previous->has_value()) {
            return result;
        }
        result.push_back(previous->value());
    }
}

TEST(BtreeScanBoundsTest, PrefixEqualityAndNextColumnRangeStopAtTheirUpperBoundary) {
    auto directory = test_support::TestDirectory::create("btree-prefix-range");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_composite_tree(directory.value(), base::RelationId{640});
    ASSERT_TRUE(tree) << tree.error().to_string();
    ASSERT_TRUE(populate(tree.value()));

    auto full = BtreeScan::for_full(tree->relation, {});
    ASSERT_TRUE(full) << full.error().to_string();
    auto all = collect_forward(full.value(), *tree->pool);
    ASSERT_TRUE(all) << all.error().to_string();
    ASSERT_EQ(all->size(), 480U);
    const auto full_pages = full->leaf_pages_visited();

    const std::array prefix{types::Value::integer(3)};
    auto prefix_scan = BtreeScan::for_eq(tree->relation, prefix, {});
    ASSERT_TRUE(prefix_scan) << prefix_scan.error().to_string();
    auto prefix_rows = collect_forward(prefix_scan.value(), *tree->pool);
    ASSERT_TRUE(prefix_rows) << prefix_rows.error().to_string();
    ASSERT_EQ(prefix_rows->size(), 40U);
    for (const auto rid : prefix_rows.value()) {
        EXPECT_EQ(rid.page_id(), base::PageId{103});
    }
    EXPECT_LT(prefix_scan->leaf_pages_visited(), full_pages / 2U);

    const std::array low{types::Value::integer(1)};
    const std::array high{types::Value::integer(2)};
    const BtreeScanRange range{
        .low = low,
        .high = high,
        .low_exclusive = true,
        .high_exclusive = false,
    };
    auto ranged = BtreeScan::for_eq_range(tree->relation, prefix, range, {});
    ASSERT_TRUE(ranged) << ranged.error().to_string();
    auto forward = collect_forward(ranged.value(), *tree->pool);
    ASSERT_TRUE(forward) << forward.error().to_string();
    ASSERT_EQ(forward->size(), 10U);
    EXPECT_EQ(forward->front().slot_id(), base::SlotId{21});
    EXPECT_EQ(forward->back().slot_id(), base::SlotId{30});

    auto reverse = BtreeScan::for_eq_range(tree->relation, prefix, range, {});
    ASSERT_TRUE(reverse) << reverse.error().to_string();
    auto backward = collect_backward(reverse.value(), *tree->pool);
    ASSERT_TRUE(backward) << backward.error().to_string();
    EXPECT_EQ(backward.value(), (std::vector<base::Rid>(forward->rbegin(), forward->rend())));
}

TEST(BtreeScanBoundsTest, RebindResolvesNewValuesAndReleasesAllPositionState) {
    auto directory = test_support::TestDirectory::create("btree-parameter-rescan");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_composite_tree(directory.value(), base::RelationId{641});
    ASSERT_TRUE(tree) << tree.error().to_string();
    ASSERT_TRUE(populate(tree.value()));

    const std::array first_prefix{types::Value::integer(1)};
    auto scan = BtreeScan::for_eq(tree->relation, first_prefix, {});
    ASSERT_TRUE(scan) << scan.error().to_string();
    auto first = scan->next(*tree->pool);
    ASSERT_TRUE(first) << first.error().to_string();
    ASSERT_TRUE(first->has_value());
    EXPECT_EQ(first->value().page_id(), base::PageId{101});

    const auto root_hint_uses = scan->root_hint_uses();
    const std::array rebound_prefix{types::Value::integer(9)};
    ASSERT_TRUE(scan->rebind_eq(rebound_prefix));
    EXPECT_EQ(scan->leaf_pages_visited(), 0U);
    EXPECT_EQ(scan->leaf_items_examined(), 0U);
    auto rebound = collect_forward(scan.value(), *tree->pool);
    ASSERT_TRUE(rebound) << rebound.error().to_string();
    ASSERT_EQ(rebound->size(), 40U);
    for (const auto rid : rebound.value()) {
        EXPECT_EQ(rid.page_id(), base::PageId{109});
    }
    EXPECT_GT(scan->root_hint_uses(), root_hint_uses);

    const std::array range_prefix{types::Value::integer(4), types::Value::integer(2)};
    const std::array low{types::Value::integer(1)};
    const std::array high{types::Value::integer(5)};
    ASSERT_TRUE(scan->rebind_eq_range(
        range_prefix,
        BtreeScanRange{
            .low = low,
            .high = high,
            .low_exclusive = false,
            .high_exclusive = true,
        }
    ));
    auto range_rows = collect_forward(scan.value(), *tree->pool);
    ASSERT_TRUE(range_rows) << range_rows.error().to_string();
    ASSERT_EQ(range_rows->size(), 4U);
    EXPECT_EQ(range_rows->front().slot_id(), base::SlotId{22});
    EXPECT_EQ(range_rows->back().slot_id(), base::SlotId{25});

    scan->rescan();
    auto repeated = collect_forward(scan.value(), *tree->pool);
    ASSERT_TRUE(repeated) << repeated.error().to_string();
    EXPECT_EQ(repeated.value(), range_rows.value());

    const std::array range_low{types::Value::integer(8)};
    const std::array range_high{types::Value::integer(8)};
    ASSERT_TRUE(scan->rebind_range(
        BtreeScanRange{
            .low = range_low,
            .high = range_high,
            .low_exclusive = false,
            .high_exclusive = false,
        }
    ));
    auto rebound_range = collect_forward(scan.value(), *tree->pool);
    ASSERT_TRUE(rebound_range) << rebound_range.error().to_string();
    ASSERT_EQ(rebound_range->size(), 40U);
    for (const auto rid : rebound_range.value()) {
        EXPECT_EQ(rid.page_id(), base::PageId{108});
    }
}

TEST(BtreeScanBoundsTest, InvalidRebindLeavesTheActiveDescriptorUnchanged) {
    auto directory = test_support::TestDirectory::create("btree-invalid-rebind");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_composite_tree(directory.value(), base::RelationId{642});
    ASSERT_TRUE(tree) << tree.error().to_string();
    ASSERT_TRUE(populate(tree.value()));

    const std::array prefix{types::Value::integer(6)};
    auto scan = BtreeScan::for_eq(tree->relation, prefix, {});
    ASSERT_TRUE(scan) << scan.error().to_string();
    auto first = scan->next(*tree->pool);
    ASSERT_TRUE(first);
    ASSERT_TRUE(first->has_value());

    const std::array invalid{
        types::Value::integer(1),
        types::Value::integer(2),
        types::Value::integer(3),
        types::Value::integer(4),
    };
    auto rejected = scan->rebind_eq(invalid);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kInvalidArgument);

    auto remaining = collect_forward(scan.value(), *tree->pool);
    ASSERT_TRUE(remaining) << remaining.error().to_string();
    ASSERT_EQ(remaining->size(), 39U);
    for (const auto rid : remaining.value()) {
        EXPECT_EQ(rid.page_id(), base::PageId{106});
    }
}

TEST(BtreeScanBoundsTest, CharacterSearchBoundsRetainTheirOwnTypeModifier) {
    auto directory = test_support::TestDirectory::create("btree-character-search-bound");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto tree = make_character_tree(directory.value(), base::RelationId{643});
    ASSERT_TRUE(tree) << tree.error().to_string();
    const types::TypeModifier stored_modifier{2};
    const std::array first{types::Value::character("ab", stored_modifier)};
    const std::array second{types::Value::character("ac", stored_modifier)};
    ASSERT_TRUE(
        tree->relation.insert(*tree->pool, first, base::Rid{base::PageId{1}, base::SlotId{1}})
    );
    ASSERT_TRUE(
        tree->relation.insert(*tree->pool, second, base::Rid{base::PageId{1}, base::SlotId{2}})
    );

    const std::array oversized_equality{types::Value::character("abc")};
    auto equality = BtreeScan::for_eq(tree->relation, oversized_equality, {});
    ASSERT_TRUE(equality) << equality.error().to_string();
    auto equality_rows = collect_forward(equality.value(), *tree->pool);
    ASSERT_TRUE(equality_rows) << equality_rows.error().to_string();
    EXPECT_TRUE(equality_rows->empty());

    const std::array exclusive_upper{types::Value::character("aba")};
    auto range = BtreeScan::for_range(
        tree->relation,
        BtreeScanRange{
            .low = {},
            .high = exclusive_upper,
            .low_exclusive = false,
            .high_exclusive = true,
        },
        {}
    );
    ASSERT_TRUE(range) << range.error().to_string();
    auto range_rows = collect_forward(range.value(), *tree->pool);
    ASSERT_TRUE(range_rows) << range_rows.error().to_string();
    ASSERT_EQ(range_rows->size(), 1U);
    EXPECT_EQ(range_rows->front(), (base::Rid{base::PageId{1}, base::SlotId{1}}));
}

}  // namespace
}  // namespace db::access
