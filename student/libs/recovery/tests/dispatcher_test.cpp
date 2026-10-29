#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/btree_wal.hpp"
#include "db/base/error.hpp"
#include "db/recovery/btree_wal.hpp"
#include "db/recovery/checkpoint.hpp"
#include "db/recovery/dispatcher.hpp"
#include "db/recovery/heap_wal.hpp"
#include "db/transaction/xid.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

constexpr storage::PageAddress kHeapAddress{base::RelationId{91}, base::PageId{3}};
constexpr storage::PageAddress kBtreeAddress{base::RelationId{92}, base::PageId{4}};

auto decoded(wal::WalRecord record) -> wal::DecodedWalRecord {
    return wal::DecodedWalRecord{
        .start_lsn = base::Lsn{100U},
        .end_lsn = base::Lsn{200U},
        .record = std::move(record),
    };
}

auto image_block(
    const storage::PageAddress address,
    const bool will_initialize,
    const bool standard_page
) -> wal::WalBlockReference {
    return wal::WalBlockReference{
        .relation_id = address.relation_id,
        .page_id = address.page_id,
        .will_initialize = will_initialize,
        .standard_page = standard_page,
        .full_page_image = wal::WalPageImage{},
        .delta = {},
    };
}

TEST(RecoveryDispatcherTest, RoutesValidatedSystemAndTransactionRecords) {
    RecoveryDispatcher dispatcher;
    auto payload = encode_checkpoint_payload(
        CheckpointPayload{
            .next_xid = transaction::kFirstNormalXid,
            .latest_completed_xid = base::kInvalidXid,
            .next_object_id = 1U,
        }
    );
    ASSERT_TRUE(payload) << payload.error().to_string();

    auto checkpoint = dispatcher.dispatch(decoded(
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::kInvalidXid,
            .resource_manager = wal::WalResourceManager::kSystem,
            .info = wal::opcode::system::kCheckpoint,
            .blocks = {},
            .main_payload = std::move(payload).value(),
        }
    ));
    ASSERT_TRUE(checkpoint) << checkpoint.error().to_string();
    EXPECT_EQ(checkpoint->route, RecoveryRecordRoute::kCheckpoint);
    EXPECT_TRUE(checkpoint->blocks.empty());

    auto committed = dispatcher.dispatch(decoded(
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{7U},
            .resource_manager = wal::WalResourceManager::kTransaction,
            .info = wal::opcode::transaction::kCommit,
            .blocks = {},
            .main_payload = {},
        }
    ));
    ASSERT_TRUE(committed) << committed.error().to_string();
    EXPECT_EQ(committed->route, RecoveryRecordRoute::kTransactionCommit);

    auto malformed = decoded(
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::kInvalidXid,
            .resource_manager = wal::WalResourceManager::kTransaction,
            .info = wal::opcode::transaction::kAbort,
            .blocks = {},
            .main_payload = {},
        }
    );
    auto rejected = dispatcher.dispatch(malformed);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);

    auto unknown = malformed;
    // Fixed-width corruption input deliberately carries an unknown wire value.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    unknown.record.resource_manager = static_cast<wal::WalResourceManager>(0xFFU);
    rejected = dispatcher.dispatch(unknown);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
}

TEST(RecoveryDispatcherTest, ValidatesHeapInitializationAndItsLsnBoundary) {
    RecoveryDispatcher dispatcher;
    auto payload = encode_heap_wal_payload(
        HeapWalPayload{
            .operation = access::HeapWalOperation::kInitialize,
            .page_flags = 0U,
            .tuples = {},
            .redirects = {},
            .dead = {},
            .unused = {},
        }
    );
    ASSERT_TRUE(payload) << payload.error().to_string();

    auto record = decoded(
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{8U},
            .resource_manager = wal::WalResourceManager::kHeap,
            .info = wal::opcode::heap::kInitialize,
            .blocks = {wal::WalBlockReference{
                .relation_id = kHeapAddress.relation_id,
                .page_id = kHeapAddress.page_id,
                .will_initialize = true,
                .standard_page = true,
                .full_page_image = std::nullopt,
                .delta = std::move(payload).value(),
            }},
            .main_payload = {},
        }
    );
    auto dispatch = dispatcher.dispatch(record);
    ASSERT_TRUE(dispatch) << dispatch.error().to_string();
    EXPECT_EQ(dispatch->route, RecoveryRecordRoute::kHeap);
    ASSERT_EQ(dispatch->blocks.size(), 1U);
    EXPECT_TRUE(dispatch->blocks.front().will_initialize);
    const auto* heap_payload = std::get_if<HeapWalPayload>(&dispatch->blocks.front().payload);
    ASSERT_NE(heap_payload, nullptr);
    EXPECT_EQ(heap_payload->operation, access::HeapWalOperation::kInitialize);
    EXPECT_EQ(
        RecoveryDispatcher::delta_disposition(dispatch.value(), 0U, base::Lsn{900U}).value(),
        RecoveryDeltaDisposition::kApply
    );

    record.record.blocks.front().will_initialize = false;
    auto rejected = dispatcher.dispatch(record);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
}

TEST(RecoveryDispatcherTest, AcceptsAnInitializedHeapMultiInsertImage) {
    RecoveryDispatcher dispatcher;
    auto record = decoded(
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{8U},
            .resource_manager = wal::WalResourceManager::kHeap,
            .info = wal::opcode::heap::kMultiInsert,
            .blocks = {wal::WalBlockReference{
                .relation_id = kHeapAddress.relation_id,
                .page_id = kHeapAddress.page_id,
                .will_initialize = true,
                .standard_page = true,
                .full_page_image = wal::WalPageImage{},
                .delta = {},
            }},
            .main_payload = {},
        }
    );

    auto dispatch = dispatcher.dispatch(record);
    ASSERT_TRUE(dispatch) << dispatch.error().to_string();
    ASSERT_EQ(dispatch->blocks.size(), 1U);
    EXPECT_TRUE(dispatch->blocks.front().will_initialize);

    auto metadata = record;
    metadata.record.info = wal::opcode::heap::kLock;
    auto rejected = dispatcher.dispatch(metadata);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
}

TEST(RecoveryDispatcherTest, ValidatesBtreeRolesAndPlansIdempotentDeltas) {
    RecoveryDispatcher dispatcher;
    const BtreeWalPayload payload{
        .operation = access::BtreeWalOperation::kInsertLeaf,
        .role = access::BtreeWalPageRole::kPrimary,
        .patches = wal::PageDelta::create(
                       std::array{wal::PageDeltaPatch{
                           .offset = 64U,
                           .bytes = {std::byte{0x5A}},
                       }}
        ).value(),
    };
    auto encoded = encode_btree_wal_payload(payload);
    ASSERT_TRUE(encoded) << encoded.error().to_string();

    auto delta_record = decoded(
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{9U},
            .resource_manager = wal::WalResourceManager::kBtree,
            .info = wal::opcode::btree::kInsertLeaf,
            .blocks = {wal::WalBlockReference{
                .relation_id = kBtreeAddress.relation_id,
                .page_id = kBtreeAddress.page_id,
                .will_initialize = false,
                .standard_page = true,
                .full_page_image = std::nullopt,
                .delta = std::move(encoded).value(),
            }},
            .main_payload = {},
        }
    );
    auto delta_dispatch = dispatcher.dispatch(delta_record);
    ASSERT_TRUE(delta_dispatch) << delta_dispatch.error().to_string();
    const auto* btree_payload =
        std::get_if<BtreeWalPayload>(&delta_dispatch->blocks.front().payload);
    ASSERT_NE(btree_payload, nullptr);
    EXPECT_EQ(*btree_payload, payload);
    EXPECT_EQ(
        RecoveryDispatcher::delta_disposition(delta_dispatch.value(), 0U, base::Lsn{199U}).value(),
        RecoveryDeltaDisposition::kApply
    );
    EXPECT_EQ(
        RecoveryDispatcher::delta_disposition(delta_dispatch.value(), 0U, base::Lsn{200U}).value(),
        RecoveryDeltaDisposition::kSkipByPageLsn
    );

    auto split = decoded(
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{9U},
            .resource_manager = wal::WalResourceManager::kBtree,
            .info = wal::opcode::btree::kSplit,
            .blocks =
                {
                    image_block(kBtreeAddress, false, true),
                    image_block(
                        storage::PageAddress{kBtreeAddress.relation_id, base::PageId{5U}},
                        true,
                        true
                    ),
                },
            .main_payload = {},
        }
    );
    auto split_dispatch = dispatcher.dispatch(split);
    ASSERT_TRUE(split_dispatch) << split_dispatch.error().to_string();
    EXPECT_EQ(split_dispatch->route, RecoveryRecordRoute::kBtree);
    ASSERT_EQ(split_dispatch->blocks.size(), 2U);
    EXPECT_TRUE(split_dispatch->blocks[1].will_initialize);

    split.record.blocks[1].will_initialize = false;
    auto rejected = dispatcher.dispatch(split);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
}

TEST(RecoveryDispatcherTest, FullImageWithDeltaBypassesTheOrdinaryLsnSkip) {
    RecoveryDispatcher dispatcher;
    auto encoded = encode_btree_wal_payload(
        BtreeWalPayload{
            .operation = access::BtreeWalOperation::kInsertLeaf,
            .role = access::BtreeWalPageRole::kPrimary,
            .patches = wal::PageDelta::create(
                           std::array{wal::PageDeltaPatch{
                               .offset = 128U,
                               .bytes = {std::byte{0x01}},
                           }}
            ).value(),
        }
    );
    ASSERT_TRUE(encoded) << encoded.error().to_string();

    auto record = decoded(
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{10U},
            .resource_manager = wal::WalResourceManager::kBtree,
            .info = wal::opcode::btree::kInsertLeaf,
            .blocks = {wal::WalBlockReference{
                .relation_id = kBtreeAddress.relation_id,
                .page_id = kBtreeAddress.page_id,
                .will_initialize = false,
                .standard_page = true,
                .full_page_image = wal::WalPageImage{},
                .delta = std::move(encoded).value(),
            }},
            .main_payload = {},
        }
    );
    auto dispatch = dispatcher.dispatch(record);
    ASSERT_TRUE(dispatch) << dispatch.error().to_string();
    ASSERT_EQ(dispatch->blocks.size(), 1U);
    EXPECT_TRUE(dispatch->blocks.front().has_full_page_image);
    EXPECT_TRUE(dispatch->blocks.front().has_delta);
    EXPECT_EQ(
        RecoveryDispatcher::delta_disposition(dispatch.value(), 0U, base::Lsn{900U}).value(),
        RecoveryDeltaDisposition::kApply
    );
}

TEST(RecoveryDispatcherTest, RoutesCatalogAtTheDeferredPhysicalBoundary) {
    RecoveryDispatcher dispatcher;
    auto record = decoded(
        wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::kInvalidXid,
            .resource_manager = wal::WalResourceManager::kCatalog,
            .info = wal::opcode::catalog::kCreate,
            .blocks = {image_block(kHeapAddress, true, true)},
            .main_payload = {},
        }
    );
    auto dispatch = dispatcher.dispatch(record);
    ASSERT_TRUE(dispatch) << dispatch.error().to_string();
    EXPECT_EQ(dispatch->route, RecoveryRecordRoute::kCatalog);

    record.record.main_payload = {std::byte{0x01}};
    auto opaque_payload = dispatcher.dispatch(record);
    ASSERT_TRUE(opaque_payload) << opaque_payload.error().to_string();

    record.record.info = 0xFFU;
    auto rejected = dispatcher.dispatch(record);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
}

}  // namespace
}  // namespace db::recovery
