#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_page.hpp"
#include "db/base/error.hpp"
#include "db/storage/page.hpp"

#include "btree_page_read_view.hpp"

namespace db::access {
namespace {

constexpr storage::PageAddress kMetapageAddress{
    .relation_id = base::RelationId{81},
    .page_id = base::PageId{0},
};
constexpr storage::PageAddress kLeafAddress{
    .relation_id = base::RelationId{81},
    .page_id = base::PageId{4},
};
constexpr storage::PageAddress kInternalAddress{
    .relation_id = base::RelationId{81},
    .page_id = base::PageId{5},
};

auto integer_layout() -> BtreeKeyLayout {
    auto layout = BtreeKeyLayout::create({types::kInt64Type});
    EXPECT_TRUE(layout) << layout.error().to_string();
    return std::move(layout).value();
}

auto make_page(const storage::PageKind kind, const storage::PageAddress address) -> storage::Page {
    auto page = storage::Page::create(kind, address);
    EXPECT_TRUE(page) << page.error().to_string();
    return std::move(page).value();
}

auto integer_key(const BtreeKeyLayout& layout, const std::int64_t value) -> EncodedBtreeKey {
    auto key = layout.encode({types::Value::integer(value)});
    EXPECT_TRUE(key) << key.error().to_string();
    return std::move(key).value();
}

TEST(BtreeMetaPageTest, PersistsKeySignatureAndRootGeneration) {
    const auto layout = integer_layout();
    auto page = make_page(storage::PageKind::kMetadata, kMetapageAddress);
    auto metapage = BtreeMetaPage::initialize(page, kMetapageAddress, layout);
    ASSERT_TRUE(metapage) << metapage.error().to_string();
    EXPECT_FALSE(metapage->root().value().has_value());
    EXPECT_EQ(metapage->root_generation().value(), 0U);

    ASSERT_TRUE(metapage->set_root(base::PageId{9}, 2U));
    const auto root = metapage->root();
    ASSERT_TRUE(root);
    ASSERT_TRUE(root->has_value());
    EXPECT_EQ(root->value().page_id, base::PageId{9});
    EXPECT_EQ(root->value().level, 2U);
    EXPECT_EQ(root->value().generation, 1U);

    auto reopened = BtreeMetaPage::open(page, kMetapageAddress, layout);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->root().value(), root.value());
    ASSERT_TRUE(reopened->clear_root());
    EXPECT_FALSE(reopened->root().value().has_value());
    EXPECT_EQ(reopened->root_generation().value(), 2U);
}

TEST(BtreePageTest, ReadOnlyOpenRejectsMutationWithoutCastingAwayConstness) {
    const auto layout = integer_layout();
    auto metadata = make_page(storage::PageKind::kMetadata, kMetapageAddress);
    ASSERT_TRUE(BtreeMetaPage::initialize(metadata, kMetapageAddress, layout));
    const storage::Page& read_only_metadata = metadata;
    auto readonly_meta = BtreeMetaPage::open(read_only_metadata, kMetapageAddress, layout);
    ASSERT_TRUE(readonly_meta) << readonly_meta.error().to_string();
    const auto set_root = readonly_meta->set_root(base::PageId{9}, 0U);
    ASSERT_FALSE(set_root);
    EXPECT_EQ(set_root.error().code(), base::ErrorCode::kInvalidArgument);

    auto data = make_page(storage::PageKind::kBtree, kLeafAddress);
    auto mutable_leaf = BtreePage::initialize(data, kLeafAddress, layout, BtreePageType::kLeaf, 0U);
    ASSERT_TRUE(mutable_leaf);
    const auto key = integer_key(layout, 7);
    const base::Rid rid{base::PageId{9}, base::SlotId{3}};
    ASSERT_TRUE(mutable_leaf->append_leaf_entry(key, rid));
    const auto expected_free_space = mutable_leaf->free_space();
    const storage::Page& read_only_data = data;
    auto readonly_leaf = BtreePage::open(read_only_data, kLeafAddress, layout);
    ASSERT_TRUE(readonly_leaf) << readonly_leaf.error().to_string();
    EXPECT_EQ(readonly_leaf->type(), BtreePageType::kLeaf);
    EXPECT_EQ(readonly_leaf->level(), 0U);
    EXPECT_FALSE(readonly_leaf->is_root());
    auto links = readonly_leaf->links();
    ASSERT_TRUE(links);
    EXPECT_EQ(links.value(), (BtreePageLinks{}));
    EXPECT_EQ(readonly_leaf->tuple_count(), 1U);
    EXPECT_EQ(readonly_leaf->free_space(), expected_free_space);
    auto tuple = readonly_leaf->tuple(0U);
    ASSERT_TRUE(tuple);
    EXPECT_EQ(tuple->kind, BtreeTupleKind::kLeafEntry);
    EXPECT_EQ(tuple->heap_rid, rid);
    EXPECT_TRUE(std::equal(tuple->key.begin(), tuple->key.end(), key.begin(), key.end()));
    const auto append = readonly_leaf->append_leaf_entry(
        integer_key(layout, 1),
        base::Rid{base::PageId{9}, base::SlotId{0}}
    );
    ASSERT_FALSE(append);
    EXPECT_EQ(append.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(BtreePageReadViewTest, BorrowsSelectedTupleFieldsFromResidentPage) {
    const auto layout = integer_layout();
    auto page = make_page(storage::PageKind::kBtree, kLeafAddress);
    auto leaf = BtreePage::initialize(page, kLeafAddress, layout, BtreePageType::kLeaf, 0U);
    ASSERT_TRUE(leaf);
    const auto key = integer_key(layout, 7);
    const auto rid = base::Rid{base::PageId{9}, base::SlotId{3}};
    ASSERT_TRUE(leaf->append_leaf_entry(key, rid));

    const auto view = BtreePageReadView::borrow(page);
    EXPECT_EQ(view.type(), BtreePageType::kLeaf);
    EXPECT_EQ(view.level(), 0U);
    EXPECT_EQ(view.tuple_count(), 1U);
    EXPECT_EQ(view.key_ref(0U).kind, BtreeTupleKind::kLeafEntry);
    EXPECT_EQ(view.tuple_ref(0U).heap_rid(), rid);
}

TEST(BtreePageTest, LeafTuplesRoundTripInSortedHighKeyBoundedOrder) {
    const auto layout = integer_layout();
    auto page = make_page(storage::PageKind::kBtree, kLeafAddress);
    auto leaf = BtreePage::initialize(page, kLeafAddress, layout, BtreePageType::kLeaf, 0U);
    ASSERT_TRUE(leaf) << leaf.error().to_string();

    const auto high_key = integer_key(layout, 10);
    const auto one = integer_key(layout, 1);
    const auto four = integer_key(layout, 4);
    ASSERT_TRUE(leaf->append_high_key(high_key));
    ASSERT_TRUE(leaf->append_leaf_entry(one, base::Rid{base::PageId{11}, base::SlotId{0}}));
    ASSERT_TRUE(leaf->append_leaf_entry(four, base::Rid{base::PageId{11}, base::SlotId{1}}));
    EXPECT_EQ(leaf->tuple_count(), 3U);
    EXPECT_EQ(leaf->tuple(0U)->kind, BtreeTupleKind::kHighKey);
    EXPECT_EQ(leaf->tuple(1U)->kind, BtreeTupleKind::kLeafEntry);
    EXPECT_EQ(leaf->tuple(2U)->heap_rid, (base::Rid{base::PageId{11}, base::SlotId{1}}));

    ASSERT_TRUE(leaf->set_links({.previous = base::PageId{2}, .next = base::PageId{6}}));
    auto reopened = BtreePage::open(page, kLeafAddress, layout);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(
        reopened->links().value(),
        (BtreePageLinks{
            .previous = base::PageId{2},
            .next = base::PageId{6},
        })
    );
}

TEST(BtreePageTest, InsertsLeafTupleAtAnOrderedSlotAtomically) {
    const auto layout = integer_layout();
    auto page = make_page(storage::PageKind::kBtree, kLeafAddress);
    auto leaf = BtreePage::initialize(page, kLeafAddress, layout, BtreePageType::kLeaf, 0U);
    ASSERT_TRUE(leaf) << leaf.error().to_string();

    ASSERT_TRUE(leaf->append_high_key(integer_key(layout, 10)));
    ASSERT_TRUE(leaf->append_leaf_entry(
        integer_key(layout, 1),
        base::Rid{base::PageId{11}, base::SlotId{1}}
    ));
    ASSERT_TRUE(leaf->append_leaf_entry(
        integer_key(layout, 7),
        base::Rid{base::PageId{11}, base::SlotId{7}}
    ));
    const auto four = integer_key(layout, 4);
    const base::Rid four_rid{base::PageId{11}, base::SlotId{4}};
    ASSERT_TRUE(leaf->insert_leaf_entry(2U, four, four_rid));
    ASSERT_EQ(leaf->tuple_count(), 4U);
    EXPECT_EQ(leaf->tuple(2U)->heap_rid, four_rid);
    EXPECT_TRUE(
        std::equal(
            leaf->tuple(2U)->key.begin(),
            leaf->tuple(2U)->key.end(),
            four.begin(),
            four.end()
        )
    );
}

TEST(BtreePageTest, InsertsAnInternalSeparatorPairWithoutRebuildingExistingPayloads) {
    const auto layout = integer_layout();
    auto page = make_page(storage::PageKind::kBtree, kInternalAddress);
    auto internal =
        BtreePage::initialize(page, kInternalAddress, layout, BtreePageType::kInternal, 1U);
    ASSERT_TRUE(internal) << internal.error().to_string();

    ASSERT_TRUE(internal->append_high_key(integer_key(layout, 50)));
    ASSERT_TRUE(internal->append_first_child(base::PageId{7}));
    ASSERT_TRUE(internal->append_separator_and_child(integer_key(layout, 10), base::PageId{8}));
    ASSERT_TRUE(internal->append_separator_and_child(integer_key(layout, 20), base::PageId{9}));
    const auto* const ten_before = internal->tuple(2U)->key.data();
    const auto* const twenty_before = internal->tuple(4U)->key.data();

    ASSERT_TRUE(
        internal->insert_separator_and_child(4U, integer_key(layout, 15), base::PageId{10})
    );
    EXPECT_EQ(internal->tuple_count(), 8U);
    EXPECT_EQ(internal->tuple(1U)->kind, BtreeTupleKind::kChildPage);
    EXPECT_EQ(internal->tuple(2U)->kind, BtreeTupleKind::kSeparator);
    EXPECT_EQ(internal->tuple(3U)->child_page_id, base::PageId{8});
    EXPECT_EQ(internal->tuple(5U)->child_page_id, base::PageId{10});
    EXPECT_EQ(internal->tuple(7U)->child_page_id, base::PageId{9});
    EXPECT_EQ(internal->tuple(2U)->key.data(), ten_before);
    EXPECT_EQ(internal->tuple(6U)->key.data(), twenty_before);
}

TEST(BtreePageTest, RefusesAnOversizedAppendWithoutChangingThePage) {
    const auto layout = integer_layout();
    auto page = make_page(storage::PageKind::kBtree, kLeafAddress);
    auto leaf = BtreePage::initialize(page, kLeafAddress, layout, BtreePageType::kLeaf, 0U);
    ASSERT_TRUE(leaf);
    const auto key = integer_key(layout, 1);

    bool exhausted = false;
    for (std::size_t attempt = 0U; attempt < 500U; ++attempt) {
        const auto before = page.bytes();
        const auto appended = leaf->append_leaf_entry(
            key,
            base::Rid{base::PageId{12}, base::SlotId{static_cast<std::uint16_t>(attempt)}}
        );
        if (!appended) {
            EXPECT_EQ(appended.error().code(), base::ErrorCode::kResourceExhausted);
            EXPECT_EQ(page.bytes(), before);
            exhausted = true;
            break;
        }
    }
    EXPECT_TRUE(exhausted);
}

}  // namespace
}  // namespace db::access
