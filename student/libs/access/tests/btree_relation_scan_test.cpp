#include <array>
#include <cstddef>
#include <cstdint>
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
#include "db/test_support/test_directory.hpp"
#include "db/transaction/ssi.hpp"

#include "btree_test_log.hpp"

namespace db::access {
namespace {

struct LeafEntry final {
    std::int64_t value{0};
    base::Rid rid{base::kInvalidRid};
};

struct SearchTreePages final {
    base::PageId left{base::kInvalidPageId};
    base::PageId right{base::kInvalidPageId};
    base::PageId root{base::kInvalidPageId};
};

constexpr auto heap_rid(const std::uint32_t page_id, const std::uint16_t slot_id) noexcept
    -> base::Rid {
    return base::Rid{base::PageId{page_id}, base::SlotId{slot_id}};
}

auto make_pool(const test_support::TestDirectory& directory, const std::size_t frames = 8U)
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

auto integer_key(const BtreeKeyLayout& layout, const std::int64_t value)
    -> base::Result<EncodedBtreeKey> {
    return layout.encode({types::Value::integer(value)});
}

void store_u16_le(
    const base::MutableBytes bytes,
    const std::size_t offset,
    const std::uint16_t value
) noexcept {
    bytes[offset] = static_cast<std::byte>(static_cast<std::uint8_t>(value));
    bytes[offset + 1U] = static_cast<std::byte>(static_cast<std::uint8_t>(value >> 8U));
}

void store_u32_le(
    const base::MutableBytes bytes,
    const std::size_t offset,
    const std::uint32_t value
) noexcept {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

auto allocate_btree_page(storage::BufferPool& pool, const base::RelationId relation_id)
    -> base::Result<base::PageId> {
    auto pin = pool.allocate_page(relation_id, storage::PageKind::kBtree);
    if (!pin) {
        return base::fail(std::move(pin).error());
    }
    const auto address = pin->address();
    if (!address.has_value()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvariantViolation,
                "B-tree allocation returned a pin without a page address",
            }
        );
    }
    if (address->page_id == base::PageId{0}) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvariantViolation,
                "B-tree data page allocated at the metapage address",
            }
        );
    }
    return address->page_id;
}

auto initialize_leaf(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const base::PageId page_id,
    const BtreeKeyLayout& layout,
    const BtreePageLinks links,
    const std::optional<std::int64_t> high_key,
    const std::span<const LeafEntry> entries
) -> base::Status {
    const storage::PageAddress address{relation_id, page_id};
    auto write = pool.write_page(address);
    if (!write) {
        return base::fail(std::move(write).error());
    }
    auto leaf = BtreePage::initialize(*write->page(), address, layout, BtreePageType::kLeaf, 0U);
    if (!leaf) {
        return base::fail(std::move(leaf).error());
    }
    if (high_key.has_value()) {
        auto encoded = integer_key(layout, high_key.value());
        if (!encoded) {
            return base::fail(std::move(encoded).error());
        }
        auto appended = leaf->append_high_key(encoded.value());
        if (!appended) {
            return appended;
        }
    }
    for (const auto& entry : entries) {
        auto encoded = integer_key(layout, entry.value);
        if (!encoded) {
            return base::fail(std::move(encoded).error());
        }
        auto appended = leaf->append_leaf_entry(encoded.value(), entry.rid);
        if (!appended) {
            return appended;
        }
    }
    return leaf->set_links(links);
}

auto initialize_root(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const SearchTreePages pages,
    const BtreeKeyLayout& layout,
    const std::int64_t separator_value
) -> base::Status {
    const storage::PageAddress address{relation_id, pages.root};
    auto write = pool.write_page(address);
    if (!write) {
        return base::fail(std::move(write).error());
    }
    auto root =
        BtreePage::initialize(*write->page(), address, layout, BtreePageType::kInternal, 1U, true);
    if (!root) {
        return base::fail(std::move(root).error());
    }
    auto first_child = root->append_first_child(pages.left);
    if (!first_child) {
        return first_child;
    }
    auto separator = integer_key(layout, separator_value);
    if (!separator) {
        return base::fail(std::move(separator).error());
    }
    return root->append_separator_and_child(separator.value(), pages.right);
}

auto publish_root(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const BtreeKeyLayout& layout,
    const base::PageId root_page_id
) -> base::Status {
    const storage::PageAddress address{relation_id, base::PageId{0}};
    auto write = pool.write_page(address);
    if (!write) {
        return base::fail(std::move(write).error());
    }
    auto metapage = BtreeMetaPage::open(*write->page(), address, layout);
    if (!metapage) {
        return base::fail(std::move(metapage).error());
    }
    return metapage->set_root(root_page_id, 1U);
}

auto build_two_leaf_tree(storage::BufferPool& pool, const BtreeRelation& relation)
    -> base::Result<SearchTreePages> {
    const auto& layout = relation.key_layout();
    auto left = allocate_btree_page(pool, relation.relation_id());
    if (!left) {
        return base::fail(std::move(left).error());
    }
    auto right = allocate_btree_page(pool, relation.relation_id());
    if (!right) {
        return base::fail(std::move(right).error());
    }
    auto root = allocate_btree_page(pool, relation.relation_id());
    if (!root) {
        return base::fail(std::move(root).error());
    }
    const SearchTreePages pages{
        .left = left.value(),
        .right = right.value(),
        .root = root.value(),
    };

    const std::array left_entries{
        LeafEntry{.value = 1, .rid = heap_rid(101U, 1U)},
        LeafEntry{.value = 3, .rid = heap_rid(103U, 3U)},
        LeafEntry{.value = 4, .rid = heap_rid(104U, 4U)},
        LeafEntry{.value = 5, .rid = heap_rid(150U, 50U)},
    };
    const std::array right_entries{
        LeafEntry{.value = 5, .rid = heap_rid(105U, 5U)},
        LeafEntry{.value = 7, .rid = heap_rid(107U, 7U)},
        LeafEntry{.value = 9, .rid = heap_rid(109U, 9U)},
    };
    auto left_status = initialize_leaf(
        pool,
        relation.relation_id(),
        pages.left,
        layout,
        BtreePageLinks{.previous = std::nullopt, .next = pages.right},
        5,
        left_entries
    );
    if (!left_status) {
        return base::fail(std::move(left_status).error());
    }
    auto right_status = initialize_leaf(
        pool,
        relation.relation_id(),
        pages.right,
        layout,
        BtreePageLinks{.previous = pages.left, .next = std::nullopt},
        std::nullopt,
        right_entries
    );
    if (!right_status) {
        return base::fail(std::move(right_status).error());
    }
    auto root_status = initialize_root(pool, relation.relation_id(), pages, layout, 5);
    if (!root_status) {
        return base::fail(std::move(root_status).error());
    }
    auto published = publish_root(pool, relation.relation_id(), layout, pages.root);
    if (!published) {
        return base::fail(std::move(published).error());
    }
    return pages;
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

template <typename Result>
void expect_unavailable(const Result& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inactive_btree_scan(BtreeScan& scan, storage::BufferPool& pool) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(scan.valid());
    EXPECT_EQ(scan.leaf_pages_visited(), 0U);
    EXPECT_EQ(scan.leaf_items_examined(), 0U);
    EXPECT_EQ(scan.root_hint_uses(), 0U);
    expect_unavailable(scan.next_candidate(pool));
    expect_unavailable(scan.previous_candidate(pool));
    expect_unavailable(scan.rebind_eq({}));
    expect_unavailable(scan.rebind_range(BtreeScanRange{}));
    expect_unavailable(scan.rebind_eq_range({}, BtreeScanRange{}));
    scan.rescan();
}

void expect_inactive_btree_relation(BtreeRelation& relation, storage::BufferPool& pool) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(relation.valid());
    EXPECT_EQ(relation.relation_id(), base::kInvalidRelationId);
    EXPECT_FALSE(relation.key_layout().valid());
    EXPECT_FALSE(relation.is_unique());
    expect_unavailable(relation.root(pool));
    expect_unavailable(relation.create_empty_root(pool));
    expect_unavailable(relation.make_bulk_build_entry({}, base::kInvalidRid));
    expect_unavailable(relation.make_projected_bulk_build_entry({}, {}, base::kInvalidRid));
    expect_unavailable(relation.bulk_build(pool, {}));
    expect_unavailable(relation.bulk_delete(pool, DeadTidStore{}));
    expect_unavailable(relation.vacuum(pool));
    expect_unavailable(relation.insert(pool, {}, base::kInvalidRid));
    expect_unavailable(BtreeCleanupBatch::for_equal(relation, {}));
    expect_unavailable(BtreeScan::for_full(relation, {}));
    expect_unavailable(BtreeScan::for_eq(relation, {}, {}));
    expect_unavailable(BtreeScan::for_range(relation, BtreeScanRange{}, {}));
    expect_unavailable(BtreeScan::for_eq_range(relation, {}, BtreeScanRange{}, {}));
}

TEST(BtreeRelationTest, CreatesRootlessRelationPublishesFirstRootAndReopens) {
    auto directory = test_support::TestDirectory::create("btree-relation-create-open");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    const auto relation_id = base::RelationId{601};

    auto created =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(created) << created.error().to_string();
    EXPECT_EQ(created->relation_id(), relation_id);
    EXPECT_FALSE(created->is_unique());
    auto rootless = created->root(*pool);
    ASSERT_TRUE(rootless) << rootless.error().to_string();
    EXPECT_FALSE(rootless->has_value());

    auto root = created->create_empty_root(*pool);
    ASSERT_TRUE(root) << root.error().to_string();
    EXPECT_EQ(root->page_id, base::PageId{1});
    EXPECT_EQ(root->level, 0U);
    EXPECT_EQ(root->generation, 1U);
    auto duplicate_root = created->create_empty_root(*pool);
    ASSERT_FALSE(duplicate_root);
    EXPECT_EQ(duplicate_root.error().code(), base::ErrorCode::kAlreadyExists);
    EXPECT_EQ(pool->relation_page_count(relation_id).value(), 2U);

    auto duplicate_relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_FALSE(duplicate_relation);
    EXPECT_EQ(duplicate_relation.error().code(), base::ErrorCode::kAlreadyExists);
    EXPECT_TRUE(pool->flush_all());
    EXPECT_TRUE(pool->sync_relation(relation_id));
    pool.reset();

    auto reopened_pool_result = make_pool(directory.value());
    ASSERT_TRUE(reopened_pool_result) << reopened_pool_result.error().to_string();
    auto reopened_pool = std::move(reopened_pool_result).value();
    auto reopened =
        BtreeRelation::open(relation_id, *reopened_pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    auto reopened_root = reopened->root(*reopened_pool);
    ASSERT_TRUE(reopened_root) << reopened_root.error().to_string();
    ASSERT_TRUE(reopened_root->has_value());
    EXPECT_EQ(reopened_root->value(), root.value());

    const storage::PageAddress root_address{relation_id, root->page_id};
    auto read = reopened_pool->read_page(root_address);
    ASSERT_TRUE(read) << read.error().to_string();
    auto root_page = BtreePage::open(*read->page(), root_address, reopened->key_layout());
    ASSERT_TRUE(root_page) << root_page.error().to_string();
    EXPECT_TRUE(root_page->is_root());
    EXPECT_EQ(root_page->type(), BtreePageType::kLeaf);
    EXPECT_EQ(root_page->level(), 0U);

    auto incompatible_layout = BtreeKeyLayout::create({types::kBoolType});
    ASSERT_TRUE(incompatible_layout);
    auto mismatch = BtreeRelation::open(
        relation_id,
        *reopened_pool,
        incompatible_layout.value(),
        false,
        test::btree_log()
    );
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(BtreeRelationTest, ClassifiesLegacyAndMalformedMetapageFormatsAtOpenBoundary) {
    auto directory = test_support::TestDirectory::create("btree-relation-legacy-btm3");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    constexpr auto relation_id = base::RelationId{609};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    {
        auto write = pool->write_page({relation_id, base::PageId{0}});
        ASSERT_TRUE(write) << write.error().to_string();
        auto bytes = write->page()->mutable_view().bytes();
        constexpr std::size_t format_offset = storage::kPageHeaderSize;
        store_u32_le(bytes, format_offset, 0x334d'5442U);
        store_u16_le(bytes, format_offset + 4U, 3U);
        store_u16_le(
            bytes,
            format_offset + 6U,
            static_cast<std::uint16_t>(kBtreeMetapageHeaderSize)
        );
    }
    ASSERT_TRUE(pool->flush_all());
    ASSERT_TRUE(pool->sync_relation(relation_id));
    pool.reset();

    auto reopened_pool = make_pool(directory.value());
    ASSERT_TRUE(reopened_pool) << reopened_pool.error().to_string();
    auto rejected =
        BtreeRelation::open(relation_id, **reopened_pool, layout.value(), false, test::btree_log());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnsupported);

    {
        auto write = (*reopened_pool)->write_page({relation_id, base::PageId{0}});
        ASSERT_TRUE(write) << write.error().to_string();
        auto bytes = write->page()->mutable_view().bytes();
        constexpr std::size_t format_offset = storage::kPageHeaderSize;
        store_u32_le(bytes, format_offset, kBtreeMetapageMagic);
        store_u16_le(bytes, format_offset + 4U, kBtreeMetapageVersion);
        store_u16_le(
            bytes,
            format_offset + 6U,
            static_cast<std::uint16_t>(kBtreeMetapageHeaderSize - 1U)
        );
    }
    auto corrupted =
        BtreeRelation::open(relation_id, **reopened_pool, layout.value(), false, test::btree_log());
    ASSERT_FALSE(corrupted);
    EXPECT_EQ(corrupted.error().code(), base::ErrorCode::kCorruption);
}

TEST(BtreeRelationTest, MoveTransfersTheDescriptorAndLeavesTheSourceInert) {
    auto directory = test_support::TestDirectory::create("btree-relation-move-lifecycle");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout);
    const auto relation_id = base::RelationId{607};
    auto source =
        BtreeRelation::create(relation_id, *pool, layout.value(), true, test::btree_log()).value();

    BtreeRelation moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    EXPECT_EQ(moved.relation_id(), relation_id);
    EXPECT_TRUE(moved.is_unique());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_btree_relation(source, *pool);

    auto assigned = BtreeRelation::create(
                        base::RelationId{608},
                        *pool,
                        layout.value(),
                        false,
                        test::btree_log()
    )
                        .value();
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    EXPECT_EQ(assigned.relation_id(), relation_id);
    EXPECT_FALSE(assigned.root(*pool).value().has_value());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_btree_relation(moved, *pool);
}

TEST(BtreeScanTest, MoveTransfersTheCursorAndLeavesTheSourceInert) {
    auto directory = test_support::TestDirectory::create("btree-scan-move-lifecycle");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout);
    auto relation = BtreeRelation::create(
        base::RelationId{606},
        *pool,
        layout.value(),
        false,
        test::btree_log()
    );
    ASSERT_TRUE(relation) << relation.error().to_string();
    auto source = BtreeScan::for_full(relation.value(), {}).value();

    BtreeScan moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_btree_scan(source, *pool);

    auto assigned = BtreeScan::for_full(relation.value(), {}).value();
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    EXPECT_FALSE(assigned.next(*pool).value().has_value());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive_btree_scan(moved, *pool);
}

TEST(BtreeScanTest, RootlessScanIsEmptyAndTrustsTheResidentRootPage) {
    auto directory = test_support::TestDirectory::create("btree-rootless-scan");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout);
    const auto relation_id = base::RelationId{602};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    auto rootless_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(rootless_scan) << rootless_scan.error().to_string();
    auto rootless = collect_forward(rootless_scan.value(), *pool);
    ASSERT_TRUE(rootless) << rootless.error().to_string();
    EXPECT_TRUE(rootless->empty());
    EXPECT_EQ(rootless_scan->leaf_pages_visited(), 0U);

    auto pages = build_two_leaf_tree(*pool, relation.value());
    ASSERT_TRUE(pages) << pages.error().to_string();
    const storage::PageAddress root_address{relation_id, pages->root};
    {
        auto write = pool->write_page(root_address);
        ASSERT_TRUE(write) << write.error().to_string();
        auto root_page = BtreePage::open(*write->page(), root_address, relation->key_layout());
        ASSERT_TRUE(root_page) << root_page.error().to_string();
        ASSERT_TRUE(root_page->set_root(false));
    }

    auto stale_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(stale_scan);
    auto rows = collect_forward(stale_scan.value(), *pool);
    ASSERT_TRUE(rows) << rows.error().to_string();
    EXPECT_EQ(rows->size(), 7U);
}

TEST(BtreeScanTest, SerializableScanRecordsRelationOrResidentLeafPages) {
    auto directory = test_support::TestDirectory::create("btree-serializable-scan");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout);
    const auto relation_id = base::RelationId{606};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();

    constexpr base::Xid rootless_reader{100};
    const transaction::Snapshot rootless_snapshot{
        .xmin = rootless_reader,
        .xmax = base::Xid{201},
        .in_progress = {rootless_reader},
        .command_id = 1U,
    };
    transaction::SerializableConflictManager rootless_conflicts;
    ASSERT_TRUE(rootless_conflicts.register_transaction(rootless_reader));
    ASSERT_TRUE(rootless_conflicts.set_snapshot(rootless_reader, rootless_snapshot));
    const BtreeScanContext rootless_context{
        .own_xid = rootless_reader,
        .isolation = transaction::IsolationLevel::kSerializable,
        .serializable_conflicts = &rootless_conflicts,
    };
    auto rootless_scan = BtreeScan::for_full(relation.value(), rootless_context);
    ASSERT_TRUE(rootless_scan) << rootless_scan.error().to_string();
    auto rootless = collect_forward(rootless_scan.value(), *pool);
    ASSERT_TRUE(rootless) << rootless.error().to_string();
    EXPECT_TRUE(rootless->empty());
    EXPECT_EQ(rootless_conflicts.read_lock_count(rootless_reader), 1U);

    auto pages = build_two_leaf_tree(*pool, relation.value());
    ASSERT_TRUE(pages) << pages.error().to_string();
    constexpr base::Xid leaf_reader{101};
    const transaction::Snapshot leaf_snapshot{
        .xmin = leaf_reader,
        .xmax = base::Xid{202},
        .in_progress = {leaf_reader},
        .command_id = 1U,
    };
    transaction::SerializableConflictManager leaf_conflicts;
    ASSERT_TRUE(leaf_conflicts.register_transaction(leaf_reader));
    ASSERT_TRUE(leaf_conflicts.set_snapshot(leaf_reader, leaf_snapshot));
    const BtreeScanContext leaf_context{
        .own_xid = leaf_reader,
        .isolation = transaction::IsolationLevel::kSerializable,
        .serializable_conflicts = &leaf_conflicts,
    };
    auto leaf_scan = BtreeScan::for_full(relation.value(), leaf_context);
    ASSERT_TRUE(leaf_scan) << leaf_scan.error().to_string();
    auto leaf_rids = collect_forward(leaf_scan.value(), *pool);
    ASSERT_TRUE(leaf_rids) << leaf_rids.error().to_string();
    EXPECT_EQ(leaf_rids->size(), 7U);
    EXPECT_EQ(leaf_conflicts.read_lock_count(leaf_reader), 2U);
}

TEST(BtreeScanTest, FullScanTraversesReciprocalLeavesInBothDirections) {
    auto directory = test_support::TestDirectory::create("btree-full-scan");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout);
    const auto relation_id = base::RelationId{603};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();
    auto pages = build_two_leaf_tree(*pool, relation.value());
    ASSERT_TRUE(pages) << pages.error().to_string();

    auto forward_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(forward_scan) << forward_scan.error().to_string();
    auto forward = collect_forward(forward_scan.value(), *pool);
    ASSERT_TRUE(forward) << forward.error().to_string();
    EXPECT_EQ(
        forward.value(),
        (std::vector<base::Rid>{
            heap_rid(101U, 1U),
            heap_rid(103U, 3U),
            heap_rid(104U, 4U),
            heap_rid(150U, 50U),
            heap_rid(105U, 5U),
            heap_rid(107U, 7U),
            heap_rid(109U, 9U),
        })
    );
    EXPECT_EQ(forward_scan->leaf_pages_visited(), 2U);
    EXPECT_EQ(forward_scan->leaf_items_examined(), 7U);

    auto backward_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(backward_scan) << backward_scan.error().to_string();
    auto backward = collect_backward(backward_scan.value(), *pool);
    ASSERT_TRUE(backward) << backward.error().to_string();
    EXPECT_EQ(
        backward.value(),
        (std::vector<base::Rid>{
            heap_rid(109U, 9U),
            heap_rid(107U, 7U),
            heap_rid(105U, 5U),
            heap_rid(150U, 50U),
            heap_rid(104U, 4U),
            heap_rid(103U, 3U),
            heap_rid(101U, 1U),
        })
    );
    EXPECT_EQ(backward_scan->leaf_pages_visited(), 2U);

    auto mixed_scan = BtreeScan::for_full(relation.value(), {});
    ASSERT_TRUE(mixed_scan);
    ASSERT_TRUE(mixed_scan->next(*pool).value().has_value());
    auto mixed_direction = mixed_scan->previous(*pool);
    ASSERT_FALSE(mixed_direction);
    EXPECT_EQ(mixed_direction.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(BtreeScanTest, SeeksPointAndInclusiveOrExclusiveRanges) {
    auto directory = test_support::TestDirectory::create("btree-range-scan");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto pool_result = make_pool(directory.value());
    ASSERT_TRUE(pool_result) << pool_result.error().to_string();
    auto pool = std::move(pool_result).value();
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout);
    const auto relation_id = base::RelationId{604};
    auto relation =
        BtreeRelation::create(relation_id, *pool, layout.value(), false, test::btree_log());
    ASSERT_TRUE(relation) << relation.error().to_string();
    auto pages = build_two_leaf_tree(*pool, relation.value());
    ASSERT_TRUE(pages) << pages.error().to_string();

    const std::array equality_values{types::Value::integer(5)};
    auto equality_scan = BtreeScan::for_eq(relation.value(), equality_values, {});
    ASSERT_TRUE(equality_scan) << equality_scan.error().to_string();
    auto equality = collect_forward(equality_scan.value(), *pool);
    ASSERT_TRUE(equality) << equality.error().to_string();
    EXPECT_EQ(equality.value(), (std::vector<base::Rid>{heap_rid(150U, 50U), heap_rid(105U, 5U)}));
    EXPECT_EQ(equality_scan->leaf_pages_visited(), 2U);

    const std::array low{types::Value::integer(3)};
    const std::array high{types::Value::integer(7)};
    const BtreeScanRange inclusive{
        .low = low,
        .high = high,
        .low_exclusive = false,
        .high_exclusive = false,
    };
    auto inclusive_scan = BtreeScan::for_range(relation.value(), inclusive, {});
    ASSERT_TRUE(inclusive_scan) << inclusive_scan.error().to_string();
    auto inclusive_result = collect_forward(inclusive_scan.value(), *pool);
    ASSERT_TRUE(inclusive_result) << inclusive_result.error().to_string();
    EXPECT_EQ(
        inclusive_result.value(),
        (std::vector<base::Rid>{
            heap_rid(103U, 3U),
            heap_rid(104U, 4U),
            heap_rid(150U, 50U),
            heap_rid(105U, 5U),
            heap_rid(107U, 7U),
        })
    );

    const BtreeScanRange exclusive{
        .low = low,
        .high = high,
        .low_exclusive = true,
        .high_exclusive = true,
    };
    auto exclusive_scan = BtreeScan::for_range(relation.value(), exclusive, {});
    ASSERT_TRUE(exclusive_scan) << exclusive_scan.error().to_string();
    auto exclusive_result = collect_forward(exclusive_scan.value(), *pool);
    ASSERT_TRUE(exclusive_result) << exclusive_result.error().to_string();
    EXPECT_EQ(
        exclusive_result.value(),
        (std::vector<base::Rid>{
            heap_rid(104U, 4U),
            heap_rid(150U, 50U),
            heap_rid(105U, 5U),
        })
    );

    auto reverse_range = BtreeScan::for_range(relation.value(), inclusive, {});
    ASSERT_TRUE(reverse_range) << reverse_range.error().to_string();
    auto reverse_result = collect_backward(reverse_range.value(), *pool);
    ASSERT_TRUE(reverse_result) << reverse_result.error().to_string();
    EXPECT_EQ(
        reverse_result.value(),
        (std::vector<base::Rid>{
            heap_rid(107U, 7U),
            heap_rid(105U, 5U),
            heap_rid(150U, 50U),
            heap_rid(104U, 4U),
            heap_rid(103U, 3U),
        })
    );

    const BtreeScanRange upper_only{
        .low = std::span<const types::Value>{},
        .high = high,
        .low_exclusive = false,
        .high_exclusive = false,
    };
    auto upper_only_scan = BtreeScan::for_range(relation.value(), upper_only, {});
    ASSERT_TRUE(upper_only_scan) << upper_only_scan.error().to_string();
    auto upper_only_result = collect_forward(upper_only_scan.value(), *pool);
    ASSERT_TRUE(upper_only_result) << upper_only_result.error().to_string();
    EXPECT_EQ(
        upper_only_result.value(),
        (std::vector<base::Rid>{
            heap_rid(101U, 1U),
            heap_rid(103U, 3U),
            heap_rid(104U, 4U),
            heap_rid(150U, 50U),
            heap_rid(105U, 5U),
            heap_rid(107U, 7U),
        })
    );

    const BtreeScanRange lower_only{
        .low = low,
        .high = std::span<const types::Value>{},
        .low_exclusive = false,
        .high_exclusive = false,
    };
    auto lower_only_scan = BtreeScan::for_range(relation.value(), lower_only, {});
    ASSERT_TRUE(lower_only_scan) << lower_only_scan.error().to_string();
    auto lower_only_result = collect_backward(lower_only_scan.value(), *pool);
    ASSERT_TRUE(lower_only_result) << lower_only_result.error().to_string();
    EXPECT_EQ(
        lower_only_result.value(),
        (std::vector<base::Rid>{
            heap_rid(109U, 9U),
            heap_rid(107U, 7U),
            heap_rid(105U, 5U),
            heap_rid(150U, 50U),
            heap_rid(104U, 4U),
            heap_rid(103U, 3U),
        })
    );

    const std::array partial_values{types::Value::integer(1)};
    const auto two_column_layout = BtreeKeyLayout::create({types::kInt64Type, types::kBoolType});
    ASSERT_TRUE(two_column_layout);
    auto two_column_relation = BtreeRelation::create(
        base::RelationId{605},
        *pool,
        two_column_layout.value(),
        false,
        test::btree_log()
    );
    ASSERT_TRUE(two_column_relation) << two_column_relation.error().to_string();
    auto partial = BtreeScan::for_eq(two_column_relation.value(), partial_values, {});
    ASSERT_TRUE(partial) << partial.error().to_string();
}

}  // namespace
}  // namespace db::access
