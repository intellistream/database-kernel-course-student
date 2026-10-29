#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_key.hpp"
#include "db/access/btree_page.hpp"
#include "db/access/btree_wal.hpp"
#include "db/base/error.hpp"
#include "db/recovery/btree_wal.hpp"
#include "db/storage/page.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"
#include "db/wal/delta.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

struct LeafImages final {
    access::BtreeKeyLayout layout;
    storage::Page before;
    storage::Page after;
    storage::PageAddress address{base::kInvalidRelationId, base::kInvalidPageId};
};

auto make_leaf_images() -> LeafImages {
    auto layout = access::BtreeKeyLayout::create({types::kInt64Type});
    EXPECT_TRUE(layout) << layout.error().to_string();
    const auto address = storage::PageAddress{base::RelationId{710}, base::PageId{4}};
    auto before = storage::Page::create(storage::PageKind::kBtree, address);
    EXPECT_TRUE(before) << before.error().to_string();
    auto leaf = access::BtreePage::initialize(
        before.value(),
        address,
        layout.value(),
        access::BtreePageType::kLeaf,
        0U,
        true
    );
    EXPECT_TRUE(leaf) << leaf.error().to_string();
    auto after = before.value();
    auto changed = access::BtreePage::open(after, address, layout.value());
    EXPECT_TRUE(changed) << changed.error().to_string();
    const std::array values{types::Value::integer(42)};
    auto key = layout->encode(values);
    EXPECT_TRUE(key) << key.error().to_string();
    EXPECT_TRUE(
        changed->append_leaf_entry(key.value(), base::Rid{base::PageId{9}, base::SlotId{2}})
    );
    return LeafImages{
        .layout = std::move(layout).value(),
        .before = std::move(before).value(),
        .after = after,
        .address = address,
    };
}

auto make_leaf_payload(const LeafImages& images) -> BtreeWalPayload {
    auto patches = wal::make_page_delta(images.before.bytes(), images.after.bytes());
    EXPECT_TRUE(patches) << patches.error().to_string();
    return BtreeWalPayload{
        .operation = access::BtreeWalOperation::kInsertLeaf,
        .role = access::BtreeWalPageRole::kPrimary,
        .patches = std::move(patches).value(),
    };
}

auto make_leaf_record(
    const LeafImages& images,
    std::vector<std::byte> delta,
    std::optional<wal::WalPageImage> image = std::nullopt
) -> wal::DecodedWalRecord {
    return wal::DecodedWalRecord{
        .start_lsn = base::Lsn{100},
        .end_lsn = base::Lsn{200},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{7},
            .resource_manager = wal::WalResourceManager::kBtree,
            .info = wal::opcode::btree::kInsertLeaf,
            .blocks = {wal::WalBlockReference{
                .relation_id = images.address.relation_id,
                .page_id = images.address.page_id,
                .will_initialize = false,
                .standard_page = true,
                .full_page_image = image,
                .delta = std::move(delta),
            }},
            .main_payload = {},
        },
    };
}

auto make_structural_payload(
    const access::BtreeWalOperation operation,
    const access::BtreeWalPageRole role,
    const std::byte value
) -> std::vector<std::byte> {
    const std::array patches{wal::PageDeltaPatch{
        .offset = 128U,
        .bytes = {value},
    }};
    auto delta = wal::PageDelta::create(patches);
    EXPECT_TRUE(delta) << delta.error().to_string();
    if (!delta) {
        return {};
    }
    auto encoded = encode_btree_wal_payload(
        BtreeWalPayload{
            .operation = operation,
            .role = role,
            .patches = std::move(delta).value(),
        }
    );
    EXPECT_TRUE(encoded) << encoded.error().to_string();
    return encoded ? std::move(encoded).value() : std::vector<std::byte>{};
}

auto make_structural_image(
    const base::RelationId relation_id,
    const base::PageId page_id,
    const bool standard_page,
    const access::BtreeWalPageRole role
) -> std::optional<wal::WalPageImage> {
    auto layout = access::BtreeKeyLayout::create({types::kInt64Type});
    EXPECT_TRUE(layout) << layout.error().to_string();
    const auto address = storage::PageAddress{relation_id, page_id};
    auto page = storage::Page::create(
        standard_page ? storage::PageKind::kBtree : storage::PageKind::kMetadata,
        address
    );
    EXPECT_TRUE(page) << page.error().to_string();
    if (!layout || !page) {
        return std::nullopt;
    }
    if (standard_page) {
        auto initialized = access::BtreePage::initialize(
            page.value(),
            address,
            layout.value(),
            role == access::BtreeWalPageRole::kNewRoot ? access::BtreePageType::kInternal
                                                       : access::BtreePageType::kLeaf,
            role == access::BtreeWalPageRole::kNewRoot ? 1U : 0U,
            role == access::BtreeWalPageRole::kNewRoot
        );
        EXPECT_TRUE(initialized) << initialized.error().to_string();
        if (!initialized) {
            return std::nullopt;
        }
    } else {
        auto initialized = access::BtreeMetaPage::initialize(page.value(), address, layout.value());
        EXPECT_TRUE(initialized) << initialized.error().to_string();
        if (!initialized) {
            return std::nullopt;
        }
    }
    return page->bytes();
}

auto make_structural_block(
    const base::RelationId relation_id,
    const base::PageId page_id,
    const bool will_initialize,
    const bool standard_page,
    const access::BtreeWalOperation operation,
    const access::BtreeWalPageRole role,
    const std::byte value
) -> wal::WalBlockReference {
    return wal::WalBlockReference{
        .relation_id = relation_id,
        .page_id = page_id,
        .will_initialize = will_initialize,
        .standard_page = standard_page,
        .full_page_image = will_initialize
                               ? make_structural_image(relation_id, page_id, standard_page, role)
                               : std::optional<wal::WalPageImage>{},
        .delta = will_initialize ? std::vector<std::byte>{}
                                 : make_structural_payload(operation, role, value),
    };
}

TEST(BtreeWalCodecTest, RoundTripsVersionedPageDeltaAndRejectsDamage) {
    const auto images = make_leaf_images();
    const auto payload = make_leaf_payload(images);
    auto encoded = encode_btree_wal_payload(payload);
    ASSERT_TRUE(encoded) << encoded.error().to_string();
    auto decoded = decode_btree_wal_payload(wal::opcode::btree::kInsertLeaf, encoded.value());
    ASSERT_TRUE(decoded) << decoded.error().to_string();
    EXPECT_EQ(decoded.value(), payload);

    auto damaged = encoded.value();
    damaged.at(0U) ^= std::byte{0x01};
    auto bad_magic = decode_btree_wal_payload(wal::opcode::btree::kInsertLeaf, damaged);
    ASSERT_FALSE(bad_magic);
    EXPECT_EQ(bad_magic.error().code(), base::ErrorCode::kCorruption);

    damaged = encoded.value();
    damaged.push_back(std::byte{0});
    EXPECT_FALSE(decode_btree_wal_payload(wal::opcode::btree::kInsertLeaf, damaged));

    auto mismatched = decode_btree_wal_payload(wal::opcode::btree::kVacuum, encoded.value());
    ASSERT_FALSE(mismatched);
    EXPECT_EQ(mismatched.error().code(), base::ErrorCode::kCorruption);
}

TEST(BtreeWalRedoTest, AppliesLeafDeltaOnceAndSkipsAtRecordEndLsn) {
    const auto images = make_leaf_images();
    auto encoded = encode_btree_wal_payload(make_leaf_payload(images));
    ASSERT_TRUE(encoded) << encoded.error().to_string();
    auto record = make_leaf_record(images, std::move(encoded).value());
    auto target = images.before;

    auto applied = apply_btree_redo(record, 0U, target);
    ASSERT_TRUE(applied) << applied.error().to_string();
    EXPECT_EQ(applied.value(), BtreeRedoDisposition::kApplied);
    auto expected = images.after;
    ASSERT_TRUE(expected.set_lsn(record.end_lsn));
    EXPECT_EQ(target.bytes(), expected.bytes());

    const auto once = target.bytes();
    auto skipped = apply_btree_redo(record, 0U, target);
    ASSERT_TRUE(skipped) << skipped.error().to_string();
    EXPECT_EQ(skipped.value(), BtreeRedoDisposition::kSkipped);
    EXPECT_EQ(target.bytes(), once);
}

TEST(BtreeWalRedoTest, SplitReplayRetainsTheIncompleteMarker) {
    auto layout = access::BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    constexpr auto relation_id = base::RelationId{714};
    constexpr auto left_page_id = base::PageId{5};
    constexpr auto right_page_id = base::PageId{6};
    const storage::PageAddress left_address{relation_id, left_page_id};
    const storage::PageAddress right_address{relation_id, right_page_id};
    auto left_before = storage::Page::create(storage::PageKind::kBtree, left_address);
    ASSERT_TRUE(left_before) << left_before.error().to_string();
    auto left = access::BtreePage::initialize(
        left_before.value(),
        left_address,
        layout.value(),
        access::BtreePageType::kLeaf,
        0U
    );
    ASSERT_TRUE(left) << left.error().to_string();
    auto left_after = left_before.value();
    auto split_left = access::BtreePage::from_latched_page(left_after);
    const std::array values{types::Value::integer(42)};
    auto separator = layout->encode(values);
    ASSERT_TRUE(separator) << separator.error().to_string();
    ASSERT_TRUE(split_left.append_high_key(separator.value()));
    ASSERT_TRUE(split_left.set_links({.previous = std::nullopt, .next = right_page_id}));
    ASSERT_TRUE(split_left.set_incomplete_split(true));

    auto right_after = storage::Page::create(storage::PageKind::kBtree, right_address);
    ASSERT_TRUE(right_after) << right_after.error().to_string();
    auto split_right = access::BtreePage::initialize(
        right_after.value(),
        right_address,
        layout.value(),
        access::BtreePageType::kLeaf,
        0U
    );
    ASSERT_TRUE(split_right) << split_right.error().to_string();
    ASSERT_TRUE(split_right->set_links({.previous = left_page_id, .next = std::nullopt}));

    auto left_delta = wal::make_page_delta(left_before->bytes(), left_after.bytes());
    ASSERT_TRUE(left_delta) << left_delta.error().to_string();
    auto left_payload = encode_btree_wal_payload(
        BtreeWalPayload{
            .operation = access::BtreeWalOperation::kSplit,
            .role = access::BtreeWalPageRole::kSplitLeft,
            .patches = std::move(left_delta).value(),
        }
    );
    ASSERT_TRUE(left_payload) << left_payload.error().to_string();
    const wal::DecodedWalRecord record{
        .start_lsn = base::Lsn{100U},
        .end_lsn = base::Lsn{200U},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{8},
            .resource_manager = wal::WalResourceManager::kBtree,
            .info = wal::opcode::btree::kSplit,
            .blocks =
                {
                    wal::WalBlockReference{
                        .relation_id = relation_id,
                        .page_id = left_page_id,
                        .will_initialize = false,
                        .standard_page = true,
                        .full_page_image = std::nullopt,
                        .delta = std::move(left_payload).value(),
                    },
                    wal::WalBlockReference{
                        .relation_id = relation_id,
                        .page_id = right_page_id,
                        .will_initialize = true,
                        .standard_page = true,
                        .full_page_image = right_after->bytes(),
                        .delta = {},
                    },
                },
            .main_payload = {},
        },
    };

    auto replayed_left = left_before.value();
    auto replayed_right = storage::Page::create(storage::PageKind::kBtree, right_address);
    ASSERT_TRUE(replayed_right) << replayed_right.error().to_string();
    ASSERT_TRUE(apply_btree_redo(record, 0U, replayed_left));
    ASSERT_TRUE(apply_btree_redo(record, 1U, replayed_right.value()));
    auto recovered_left = access::BtreePage::open(replayed_left, left_address, layout.value());
    ASSERT_TRUE(recovered_left) << recovered_left.error().to_string();
    EXPECT_TRUE(recovered_left->incomplete_split());
    EXPECT_EQ(recovered_left->links_ref().next, std::optional{right_page_id});
}

TEST(BtreeWalRedoTest, FinishSplitRequiresAndReplaysParentAndChildTogether) {
    auto layout = access::BtreeKeyLayout::create({types::kInt64Type});
    ASSERT_TRUE(layout) << layout.error().to_string();
    constexpr auto relation_id = base::RelationId{713};
    constexpr auto parent_page_id = base::PageId{4};
    constexpr auto split_child_page_id = base::PageId{5};
    constexpr auto right_page_id = base::PageId{6};
    const storage::PageAddress parent_address{relation_id, parent_page_id};
    const storage::PageAddress child_address{relation_id, split_child_page_id};

    auto parent_before = storage::Page::create(storage::PageKind::kBtree, parent_address);
    ASSERT_TRUE(parent_before) << parent_before.error().to_string();
    auto parent = access::BtreePage::initialize(
        parent_before.value(),
        parent_address,
        layout.value(),
        access::BtreePageType::kInternal,
        1U,
        true
    );
    ASSERT_TRUE(parent) << parent.error().to_string();
    ASSERT_TRUE(parent->append_first_child(split_child_page_id));

    auto child_before = storage::Page::create(storage::PageKind::kBtree, child_address);
    ASSERT_TRUE(child_before) << child_before.error().to_string();
    auto child = access::BtreePage::initialize(
        child_before.value(),
        child_address,
        layout.value(),
        access::BtreePageType::kLeaf,
        0U
    );
    ASSERT_TRUE(child) << child.error().to_string();
    const std::array values{types::Value::integer(42)};
    auto separator = layout->encode(values);
    ASSERT_TRUE(separator) << separator.error().to_string();
    ASSERT_TRUE(child->append_high_key(separator.value()));
    ASSERT_TRUE(child->set_links({.previous = std::nullopt, .next = right_page_id}));
    ASSERT_TRUE(child->set_incomplete_split(true));

    auto parent_after = parent_before.value();
    auto changed_parent = access::BtreePage::from_latched_page(parent_after);
    ASSERT_TRUE(changed_parent.append_separator_and_child(separator.value(), right_page_id));
    auto child_after = child_before.value();
    auto changed_child = access::BtreePage::from_latched_page(child_after);
    ASSERT_TRUE(changed_child.set_incomplete_split(false));

    auto parent_delta = wal::make_page_delta(parent_before->bytes(), parent_after.bytes());
    ASSERT_TRUE(parent_delta) << parent_delta.error().to_string();
    auto child_delta = wal::make_page_delta(child_before->bytes(), child_after.bytes());
    ASSERT_TRUE(child_delta) << child_delta.error().to_string();
    auto parent_payload = encode_btree_wal_payload(
        BtreeWalPayload{
            .operation = access::BtreeWalOperation::kFinishSplit,
            .role = access::BtreeWalPageRole::kPrimary,
            .patches = std::move(parent_delta).value(),
        }
    );
    ASSERT_TRUE(parent_payload) << parent_payload.error().to_string();
    auto child_payload = encode_btree_wal_payload(
        BtreeWalPayload{
            .operation = access::BtreeWalOperation::kFinishSplit,
            .role = access::BtreeWalPageRole::kSplitChild,
            .patches = std::move(child_delta).value(),
        }
    );
    ASSERT_TRUE(child_payload) << child_payload.error().to_string();
    wal::DecodedWalRecord record{
        .start_lsn = base::Lsn{300U},
        .end_lsn = base::Lsn{400U},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{8},
            .resource_manager = wal::WalResourceManager::kBtree,
            .info = wal::opcode::btree::kFinishSplit,
            .blocks =
                {
                    wal::WalBlockReference{
                        .relation_id = relation_id,
                        .page_id = parent_page_id,
                        .will_initialize = false,
                        .standard_page = true,
                        .full_page_image = std::nullopt,
                        .delta = std::move(parent_payload).value(),
                    },
                    wal::WalBlockReference{
                        .relation_id = relation_id,
                        .page_id = split_child_page_id,
                        .will_initialize = false,
                        .standard_page = true,
                        .full_page_image = std::nullopt,
                        .delta = std::move(child_payload).value(),
                    },
                },
            .main_payload = {},
        },
    };

    auto incomplete_replay = child_before.value();
    auto replayed_child = access::BtreePage::open(incomplete_replay, child_address, layout.value());
    ASSERT_TRUE(replayed_child) << replayed_child.error().to_string();
    EXPECT_TRUE(replayed_child->incomplete_split());

    auto missing_child = record;
    missing_child.record.blocks.pop_back();
    auto rejected_parent = parent_before.value();
    const auto exact_parent_before = rejected_parent.bytes();
    auto rejected = apply_btree_redo(missing_child, 0U, rejected_parent);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(rejected_parent.bytes(), exact_parent_before);

    auto replayed_parent_page = parent_before.value();
    auto replayed_child_page = child_before.value();
    auto parent_applied = apply_btree_redo(record, 0U, replayed_parent_page);
    ASSERT_TRUE(parent_applied) << parent_applied.error().to_string();
    auto child_applied = apply_btree_redo(record, 1U, replayed_child_page);
    ASSERT_TRUE(child_applied) << child_applied.error().to_string();
    auto replayed_parent =
        access::BtreePage::open(replayed_parent_page, parent_address, layout.value());
    ASSERT_TRUE(replayed_parent) << replayed_parent.error().to_string();
    EXPECT_EQ(replayed_parent->tuple_count(), 3U);
    replayed_child = access::BtreePage::open(replayed_child_page, child_address, layout.value());
    ASSERT_TRUE(replayed_child) << replayed_child.error().to_string();
    EXPECT_FALSE(replayed_child->incomplete_split());
    EXPECT_EQ(replayed_parent_page.lsn(), record.end_lsn);
    EXPECT_EQ(replayed_child_page.lsn(), record.end_lsn);
}

TEST(BtreeWalRedoTest, AcceptsCascadingSplitSchemasAndRejectsMissingOrMisorderedBlocks) {
    constexpr auto relation_id = base::RelationId{715};
    wal::DecodedWalRecord split_finish{
        .start_lsn = base::Lsn{500U},
        .end_lsn = base::Lsn{600U},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{9},
            .resource_manager = wal::WalResourceManager::kBtree,
            .info = wal::opcode::btree::kSplitFinish,
            .blocks =
                {
                    make_structural_block(
                        relation_id,
                        base::PageId{10},
                        false,
                        true,
                        access::BtreeWalOperation::kSplitFinish,
                        access::BtreeWalPageRole::kSplitLeft,
                        std::byte{0x10}
                    ),
                    make_structural_block(
                        relation_id,
                        base::PageId{11},
                        true,
                        true,
                        access::BtreeWalOperation::kSplitFinish,
                        access::BtreeWalPageRole::kSplitRight,
                        std::byte{0x11}
                    ),
                    make_structural_block(
                        relation_id,
                        base::PageId{12},
                        false,
                        true,
                        access::BtreeWalOperation::kSplitFinish,
                        access::BtreeWalPageRole::kSplitSibling,
                        std::byte{0x12}
                    ),
                    make_structural_block(
                        relation_id,
                        base::PageId{13},
                        false,
                        true,
                        access::BtreeWalOperation::kSplitFinish,
                        access::BtreeWalPageRole::kSplitChild,
                        std::byte{0x13}
                    ),
                },
            .main_payload = {},
        },
    };
    for (std::size_t block_index = 0U; block_index < split_finish.record.blocks.size();
         ++block_index) {
        const auto& block = split_finish.record.blocks[block_index];
        auto page = storage::Page::create(
            storage::PageKind::kBtree,
            storage::PageAddress{block.relation_id, block.page_id}
        );
        ASSERT_TRUE(page) << page.error().to_string();
        auto applied = apply_btree_redo(split_finish, block_index, page.value());
        ASSERT_TRUE(applied) << applied.error().to_string();
        EXPECT_EQ(applied.value(), BtreeRedoDisposition::kApplied);
    }

    auto delta_only_initialization = split_finish;
    auto& initializing_block = delta_only_initialization.record.blocks[1U];
    initializing_block.full_page_image.reset();
    initializing_block.delta = make_structural_payload(
        access::BtreeWalOperation::kSplitFinish,
        access::BtreeWalPageRole::kSplitRight,
        std::byte{0x11}
    );
    auto rejected_initialization = storage::Page::create(
        storage::PageKind::kBtree,
        storage::PageAddress{initializing_block.relation_id, initializing_block.page_id}
    );
    ASSERT_TRUE(rejected_initialization) << rejected_initialization.error().to_string();
    const auto initialization_before = rejected_initialization->bytes();
    auto rejected =
        apply_btree_redo(delta_only_initialization, 1U, rejected_initialization.value());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(rejected_initialization->bytes(), initialization_before);

    auto missing_child = split_finish;
    missing_child.record.blocks.pop_back();
    auto untouched = storage::Page::create(
        storage::PageKind::kBtree,
        storage::PageAddress{relation_id, base::PageId{10}}
    );
    ASSERT_TRUE(untouched) << untouched.error().to_string();
    const auto exact_before = untouched->bytes();
    rejected = apply_btree_redo(missing_child, 0U, untouched.value());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(untouched->bytes(), exact_before);

    wal::DecodedWalRecord cascading_root{
        .start_lsn = base::Lsn{600U},
        .end_lsn = base::Lsn{700U},
        .record = wal::WalRecord{
            .previous_lsn = split_finish.end_lsn,
            .xid = base::Xid{9},
            .resource_manager = wal::WalResourceManager::kBtree,
            .info = wal::opcode::btree::kNewRoot,
            .blocks =
                {
                    make_structural_block(
                        relation_id,
                        base::PageId{20},
                        false,
                        true,
                        access::BtreeWalOperation::kNewRoot,
                        access::BtreeWalPageRole::kSplitLeft,
                        std::byte{0x20}
                    ),
                    make_structural_block(
                        relation_id,
                        base::PageId{21},
                        true,
                        true,
                        access::BtreeWalOperation::kNewRoot,
                        access::BtreeWalPageRole::kSplitRight,
                        std::byte{0x21}
                    ),
                    make_structural_block(
                        relation_id,
                        base::PageId{22},
                        true,
                        true,
                        access::BtreeWalOperation::kNewRoot,
                        access::BtreeWalPageRole::kNewRoot,
                        std::byte{0x22}
                    ),
                    make_structural_block(
                        relation_id,
                        base::PageId{0},
                        false,
                        false,
                        access::BtreeWalOperation::kNewRoot,
                        access::BtreeWalPageRole::kMetadata,
                        std::byte{0x23}
                    ),
                    make_structural_block(
                        relation_id,
                        base::PageId{23},
                        false,
                        true,
                        access::BtreeWalOperation::kNewRoot,
                        access::BtreeWalPageRole::kSplitChild,
                        std::byte{0x24}
                    ),
                },
            .main_payload = {},
        },
    };
    for (std::size_t block_index = 0U; block_index < cascading_root.record.blocks.size();
         ++block_index) {
        const auto& block = cascading_root.record.blocks[block_index];
        const auto kind =
            block.standard_page ? storage::PageKind::kBtree : storage::PageKind::kMetadata;
        auto page =
            storage::Page::create(kind, storage::PageAddress{block.relation_id, block.page_id});
        ASSERT_TRUE(page) << page.error().to_string();
        auto applied = apply_btree_redo(cascading_root, block_index, page.value());
        ASSERT_TRUE(applied) << applied.error().to_string();
        EXPECT_EQ(applied.value(), BtreeRedoDisposition::kApplied);
    }

    auto misordered = cascading_root;
    std::swap(misordered.record.blocks[3U].delta, misordered.record.blocks[4U].delta);
    auto metadata = storage::Page::create(
        storage::PageKind::kMetadata,
        storage::PageAddress{relation_id, base::PageId{0}}
    );
    ASSERT_TRUE(metadata) << metadata.error().to_string();
    rejected = apply_btree_redo(misordered, 3U, metadata.value());
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
}

TEST(BtreeWalRedoTest, FullPageImageRepairsANewerMalformedTarget) {
    const auto images = make_leaf_images();
    auto image = images.after.bytes();
    auto target = images.before;
    auto bytes = target.mutable_view(200U, 1U);
    ASSERT_TRUE(bytes) << bytes.error().to_string();
    bytes->front() = std::byte{0xff};
    ASSERT_TRUE(target.set_lsn(base::Lsn{999}));
    auto record = make_leaf_record(images, {}, image);

    auto applied = apply_btree_redo(record, 0U, target);
    ASSERT_TRUE(applied) << applied.error().to_string();
    EXPECT_EQ(applied.value(), BtreeRedoDisposition::kApplied);
    auto expected = images.after;
    ASSERT_TRUE(expected.set_lsn(record.end_lsn));
    EXPECT_EQ(target.bytes(), expected.bytes());
}

TEST(BtreeWalRedoTest, FullPageImageBaseAppliesItsRetainedDelta) {
    const auto images = make_leaf_images();
    auto encoded = encode_btree_wal_payload(make_leaf_payload(images));
    ASSERT_TRUE(encoded) << encoded.error().to_string();
    auto image = images.before.bytes();
    auto record = make_leaf_record(images, std::move(encoded).value(), image);

    auto target = images.before;
    ASSERT_TRUE(target.set_lsn(base::Lsn{999U}));
    auto magic = target.mutable_view(0U, sizeof(std::uint32_t));
    ASSERT_TRUE(magic) << magic.error().to_string();
    std::fill(magic->begin(), magic->end(), std::byte{0});

    auto applied = apply_btree_redo(record, 0U, target);
    ASSERT_TRUE(applied) << applied.error().to_string();
    EXPECT_EQ(applied.value(), BtreeRedoDisposition::kApplied);
    auto expected = images.after;
    ASSERT_TRUE(expected.set_lsn(record.end_lsn));
    EXPECT_EQ(target.bytes(), expected.bytes());
}

TEST(BtreeWalRedoTest, AppliesTrustedFullPageImageWithoutTupleAudit) {
    auto images = make_leaf_images();
    auto damaged = images.after;
    auto leaf = access::BtreePage::from_latched_page(damaged, images.address, images.layout);
    const std::array values{types::Value::integer(43)};
    auto key = images.layout.encode(values);
    ASSERT_TRUE(key) << key.error().to_string();
    ASSERT_TRUE(leaf.append_leaf_entry(key.value(), base::Rid{base::PageId{9}, base::SlotId{3}}));
    auto slots = damaged.mutable_view(storage::kPageHeaderSize, 2U * access::kBtreeSlotSize);
    ASSERT_TRUE(slots) << slots.error().to_string();
    std::copy_n(slots->begin(), access::kBtreeSlotSize, slots->begin() + access::kBtreeSlotSize);

    auto record = make_leaf_record(images, {}, damaged.bytes());
    auto target = images.before;
    auto applied = apply_btree_redo(record, 0U, target);
    ASSERT_TRUE(applied) << applied.error().to_string();
    EXPECT_EQ(applied.value(), BtreeRedoDisposition::kApplied);
    ASSERT_TRUE(damaged.set_lsn(record.end_lsn));
    EXPECT_EQ(target.bytes(), damaged.bytes());
}

TEST(BtreeWalRedoTest, RejectsAnInvalidStructuralBlockRoleBeforeMutation) {
    const auto images = make_leaf_images();
    auto payload = make_leaf_payload(images);
    payload.operation = access::BtreeWalOperation::kSplit;
    payload.role = access::BtreeWalPageRole::kSplitLeft;
    auto encoded = encode_btree_wal_payload(payload);
    ASSERT_TRUE(encoded) << encoded.error().to_string();
    auto record = make_leaf_record(images, std::move(encoded).value());
    record.record.info = wal::opcode::btree::kSplit;
    const auto original = images.before.bytes();
    auto target = images.before;

    auto rejected = apply_btree_redo(record, 0U, target);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(target.bytes(), original);
}

TEST(BtreeWalAdapterTest, WalInsertionFailureRestoresTheExactBeforeImage) {
    auto directory = test_support::TestDirectory::create("btree-wal-adapter-failure");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto manager = wal::WalManager::open(
        directory->join("wal"),
        wal::WalManagerOptions{
            .system_identifier = 0x4254'5245'4546'4149ULL,
            .timeline = 1,
            .ring_page_count = 4,
            .retention_lsn = base::kInvalidLsn,
            .redo_lsn = base::kInvalidLsn,
            .create = true,
            .storage_fault_port = nullptr,
        }
    );
    ASSERT_TRUE(manager) << manager.error().to_string();
    BtreeMutationWalAdapter adapter{manager.value()};
    auto images = make_leaf_images();
    const auto exact_before = images.before.bytes();
    const std::array pages{access::BtreeWalPageMutation{
        .role = access::BtreeWalPageRole::kPrimary,
        .address = images.address,
        .will_initialize = false,
        .before_page = &images.before,
        .after_page = &images.after,
    }};

    ASSERT_TRUE(manager->shutdown());
    auto failed = adapter.log(
        access::BtreeWalMutation{
            .operation = access::BtreeWalOperation::kInsertLeaf,
            .xid = base::Xid{7},
            .pages = pages,
        }
    );
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kCancelled);
    EXPECT_EQ(images.after.bytes(), exact_before);
}

TEST(BtreeWalAdapterTest, RejectsAPageAheadOfTheWalStreamBeforeInsertion) {
    auto directory = test_support::TestDirectory::create("btree-wal-adapter-lsn");
    ASSERT_TRUE(directory) << directory.error().to_string();
    auto manager = wal::WalManager::open(
        directory->join("wal"),
        wal::WalManagerOptions{
            .system_identifier = 0x4254'5245'454c'534eULL,
            .timeline = 1,
            .ring_page_count = 4,
            .retention_lsn = base::kInvalidLsn,
            .redo_lsn = base::kInvalidLsn,
            .create = true,
            .storage_fault_port = nullptr,
        }
    );
    ASSERT_TRUE(manager) << manager.error().to_string();
    BtreeMutationWalAdapter adapter{manager.value()};
    auto images = make_leaf_images();
    const auto original_end = manager->end_lsn();
    ASSERT_TRUE(images.before.set_lsn(base::Lsn{original_end.value() + 1U}));
    const auto exact_before = images.before.bytes();
    const std::array pages{access::BtreeWalPageMutation{
        .role = access::BtreeWalPageRole::kPrimary,
        .address = images.address,
        .will_initialize = false,
        .before_page = &images.before,
        .after_page = &images.after,
    }};

    auto rejected = adapter.log(
        access::BtreeWalMutation{
            .operation = access::BtreeWalOperation::kInsertLeaf,
            .xid = base::Xid{7},
            .pages = pages,
        }
    );
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(manager->end_lsn(), original_end);
    EXPECT_EQ(images.after.bytes(), exact_before);
    EXPECT_TRUE(manager->shutdown());
}

}  // namespace
}  // namespace db::recovery
