#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/heap_page.hpp"
#include "db/access/heap_wal.hpp"
#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/recovery/heap_wal.hpp"
#include "db/storage/page.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

constexpr auto kPageId = base::PageId{5};
constexpr storage::PageAddress kAddress{base::RelationId{81}, kPageId};
constexpr std::uint64_t kSystemIdentifier = 0x0123'4567'89AB'CDEFULL;

auto bytes(const std::string_view value) -> std::vector<std::byte> {
    const auto view = std::as_bytes(std::span{value.data(), value.size()});
    return {view.begin(), view.end()};
}

auto payload_is(const base::Bytes payload, const std::string_view expected) -> bool {
    const auto expected_bytes = bytes(expected);
    return payload.size() == expected_bytes.size()
           && std::equal(payload.begin(), payload.end(), expected_bytes.begin());
}

auto tuple(const base::SlotId slot, const base::Xid xmin, const std::string_view payload)
    -> std::vector<std::byte> {
    return access::encode_heap_tuple(
               access::TupleHeader::inserted(xmin, base::Rid{kPageId, slot}, 3),
               bytes(payload)
    )
        .value();
}

auto encoded_header(access::TupleHeader header) -> std::vector<std::byte> {
    std::vector<std::byte> result(access::kTupleHeaderSize);
    EXPECT_TRUE(header.encode(result));
    return result;
}

auto info_for(const access::HeapWalOperation operation) -> std::uint8_t {
    switch (operation) {
        case access::HeapWalOperation::kInitialize:
            return wal::opcode::heap::kInitialize;
        case access::HeapWalOperation::kInsert:
            return wal::opcode::heap::kInsert;
        case access::HeapWalOperation::kMultiInsert:
            return wal::opcode::heap::kMultiInsert;
        case access::HeapWalOperation::kLock:
            return wal::opcode::heap::kLock;
        case access::HeapWalOperation::kUpdate:
            return wal::opcode::heap::kUpdate;
        case access::HeapWalOperation::kHotUpdate:
            return wal::opcode::heap::kHotUpdate;
        case access::HeapWalOperation::kDelete:
            return wal::opcode::heap::kDelete;
        case access::HeapWalOperation::kPrune:
            return wal::opcode::heap::kPrune;
    }
    return 0xFF;
}

auto redo_record(
    const HeapWalPayload& payload,
    const base::Xid xid,
    const base::Lsn start_lsn,
    const base::Lsn end_lsn,
    const bool will_initialize = false
) -> wal::DecodedWalRecord {
    return wal::DecodedWalRecord{
        .start_lsn = start_lsn,
        .end_lsn = end_lsn,
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = xid,
            .resource_manager = wal::WalResourceManager::kHeap,
            .info = info_for(payload.operation),
            .blocks = {wal::WalBlockReference{
                .relation_id = kAddress.relation_id,
                .page_id = kAddress.page_id,
                .will_initialize = will_initialize,
                .standard_page = true,
                .full_page_image = std::nullopt,
                .delta = encode_heap_wal_payload(payload).value(),
            }},
            .main_payload = {},
        },
    };
}

auto open_heap(storage::Page& page) -> access::HeapPage {
    auto heap = access::HeapPage::open(page, kAddress);
    EXPECT_TRUE(heap) << heap.error().to_string();
    return heap.value();
}

auto manager_options() -> wal::WalManagerOptions {
    return wal::WalManagerOptions{
        .system_identifier = kSystemIdentifier,
        .timeline = 1,
        .ring_page_count = 4,
        .retention_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .create = true,
        .storage_fault_port = nullptr,
    };
}

TEST(HeapWalCodecTest, VersionOnePayloadsRoundTripEveryOperation) {
    auto locked =
        access::TupleHeader::inserted(base::Xid{10}, base::Rid{kPageId, base::SlotId{1}}, 4);
    locked.xmax = base::Xid{20};
    locked.flags = access::tuple_flags::kXmaxLockOnly;

    auto updated = locked;
    updated.ctid = base::Rid{base::PageId{7}, base::SlotId{2}};
    updated.flags = access::tuple_flags::kUpdated;

    auto deleted = locked;
    deleted.ctid = base::Rid{kPageId, base::SlotId{1}};
    deleted.flags = access::tuple_flags::kDeleted;

    auto hot_old = locked;
    hot_old.ctid = base::Rid{kPageId, base::SlotId{3}};
    hot_old.flags = access::tuple_flags::kUpdated | access::tuple_flags::kHotUpdated;
    auto hot_new =
        access::TupleHeader::inserted(base::Xid{20}, base::Rid{kPageId, base::SlotId{3}}, 4);
    hot_new.flags = access::tuple_flags::kHeapOnly;
    auto hot_new_bytes = access::encode_heap_tuple(hot_new, bytes("hot")).value();

    const std::array payloads{
        std::pair{
            wal::opcode::heap::kInitialize,
            HeapWalPayload{
                .operation = access::HeapWalOperation::kInitialize,
                .page_flags = 0,
                .tuples = {},
                .redirects = {},
                .dead = {},
                .unused = {},
            },
        },
        std::pair{
            wal::opcode::heap::kInsert,
            HeapWalPayload{
                .operation = access::HeapWalOperation::kInsert,
                .page_flags = 0,
                .tuples = {HeapWalTupleData{
                    .slot = base::SlotId{0},
                    .bytes = tuple(base::SlotId{0}, base::Xid{10}, "one"),
                }},
                .redirects = {},
                .dead = {},
                .unused = {},
            },
        },
        std::pair{
            wal::opcode::heap::kMultiInsert,
            HeapWalPayload{
                .operation = access::HeapWalOperation::kMultiInsert,
                .page_flags = 0,
                .tuples =
                    {
                        HeapWalTupleData{
                            .slot = base::SlotId{0},
                            .bytes = tuple(base::SlotId{0}, base::Xid{10}, "one"),
                        },
                        HeapWalTupleData{
                            .slot = base::SlotId{1},
                            .bytes = tuple(base::SlotId{1}, base::Xid{10}, "two"),
                        },
                    },
                .redirects = {},
                .dead = {},
                .unused = {},
            },
        },
        std::pair{
            wal::opcode::heap::kLock,
            HeapWalPayload{
                .operation = access::HeapWalOperation::kLock,
                .page_flags = access::heap_page_flags::kPrunable,
                .tuples = {HeapWalTupleData{
                    .slot = base::SlotId{1},
                    .bytes = encoded_header(locked),
                }},
                .redirects = {},
                .dead = {},
                .unused = {},
            },
        },
        std::pair{
            wal::opcode::heap::kUpdate,
            HeapWalPayload{
                .operation = access::HeapWalOperation::kUpdate,
                .page_flags = access::heap_page_flags::kPrunable,
                .tuples = {HeapWalTupleData{
                    .slot = base::SlotId{1},
                    .bytes = encoded_header(updated),
                }},
                .redirects = {},
                .dead = {},
                .unused = {},
            },
        },
        std::pair{
            wal::opcode::heap::kDelete,
            HeapWalPayload{
                .operation = access::HeapWalOperation::kDelete,
                .page_flags = access::heap_page_flags::kPrunable,
                .tuples = {HeapWalTupleData{
                    .slot = base::SlotId{1},
                    .bytes = encoded_header(deleted),
                }},
                .redirects = {},
                .dead = {},
                .unused = {},
            },
        },
        std::pair{
            wal::opcode::heap::kHotUpdate,
            HeapWalPayload{
                .operation = access::HeapWalOperation::kHotUpdate,
                .page_flags = access::heap_page_flags::kPrunable,
                .tuples =
                    {
                        HeapWalTupleData{
                            .slot = base::SlotId{1},
                            .bytes = encoded_header(hot_old),
                        },
                        HeapWalTupleData{
                            .slot = base::SlotId{3},
                            .bytes = hot_new_bytes,
                        },
                    },
                .redirects = {},
                .dead = {},
                .unused = {},
            },
        },
        std::pair{
            wal::opcode::heap::kPrune,
            HeapWalPayload{
                .operation = access::HeapWalOperation::kPrune,
                .page_flags = 0,
                .tuples = {},
                .redirects = {access::HeapPruneRedirect{
                    .root = base::SlotId{0},
                    .target = base::SlotId{3},
                }},
                .dead = {base::SlotId{1}},
                .unused = {base::SlotId{2}},
            },
        },
    };

    for (const auto& [info, payload] : payloads) {
        auto encoded = encode_heap_wal_payload(payload);
        ASSERT_TRUE(encoded) << static_cast<unsigned int>(info) << ": "
                             << encoded.error().to_string();
        ASSERT_GE(encoded->size(), kHeapWalPayloadHeaderSize);
        base::LittleEndianReader reader{encoded.value()};
        EXPECT_EQ(reader.read_u32().value(), kHeapWalPayloadMagic);
        EXPECT_EQ(reader.read_u16().value(), kHeapWalPayloadVersion);
        EXPECT_EQ(reader.read_u16().value(), kHeapWalPayloadHeaderSize);
        EXPECT_EQ(decode_heap_wal_payload(info, encoded.value()).value(), payload);
    }
}

TEST(HeapWalCodecTest, RejectsMalformedAndNoncanonicalPayloads) {
    HeapWalPayload empty_insert{
        .operation = access::HeapWalOperation::kInsert,
        .page_flags = 0,
        .tuples = {},
        .redirects = {},
        .dead = {},
        .unused = {},
    };
    EXPECT_EQ(
        encode_heap_wal_payload(empty_insert).error().code(),
        base::ErrorCode::kInvalidArgument
    );

    HeapWalPayload duplicate_prune{
        .operation = access::HeapWalOperation::kPrune,
        .page_flags = 0,
        .tuples = {},
        .redirects = {access::HeapPruneRedirect{
            .root = base::SlotId{1},
            .target = base::SlotId{2},
        }},
        .dead = {base::SlotId{1}},
        .unused = {},
    };
    EXPECT_FALSE(encode_heap_wal_payload(duplicate_prune));

    auto encoded = encode_heap_wal_payload(
                       HeapWalPayload{
                           .operation = access::HeapWalOperation::kInitialize,
                           .page_flags = 0,
                           .tuples = {},
                           .redirects = {},
                           .dead = {},
                           .unused = {},
                       }
    )
                       .value();
    encoded[4] = std::byte{2};
    EXPECT_EQ(
        decode_heap_wal_payload(wal::opcode::heap::kInitialize, encoded).error().code(),
        base::ErrorCode::kCorruption
    );
    encoded = encode_heap_wal_payload(
                  HeapWalPayload{
                      .operation = access::HeapWalOperation::kInitialize,
                      .page_flags = 0,
                      .tuples = {},
                      .redirects = {},
                      .dead = {},
                      .unused = {},
                  }
    )
                  .value();
    encoded.push_back(std::byte{0});
    EXPECT_FALSE(decode_heap_wal_payload(wal::opcode::heap::kInitialize, encoded));
    EXPECT_FALSE(decode_heap_wal_payload(0xFF, encoded));
}

TEST(HeapWalAdapterTest, WritesForcedInitializationThenSemanticInsertRedo) {
    auto root = test_support::TestDirectory::create("heap-wal-adapter");
    ASSERT_TRUE(root);
    auto manager = wal::WalManager::open(root->join("wal"), manager_options());
    ASSERT_TRUE(manager);
    HeapMutationWalAdapter adapter{manager.value()};
    auto page = storage::Page::create(storage::PageKind::kHeap, kAddress).value();

    auto initialized = adapter.log(
        access::HeapWalMutation{
            .operation = access::HeapWalOperation::kInitialize,
            .xid = base::kInvalidXid,
            .address = kAddress,
            .will_initialize = true,
            .before_page = nullptr,
            .after_page = &page,
            .tuples = {},
        }
    );
    ASSERT_TRUE(initialized);
    ASSERT_TRUE(page.set_lsn(initialized.value()));

    const auto before_insert = page;
    auto heap = open_heap(page);
    const auto rid = heap.insert(base::Xid{10}, 2, bytes("row")).value();
    const auto inserted_bytes = heap.tuple_bytes(rid.slot_id()).value();
    const std::array inserted_tuples{access::HeapWalTuple{
        .slot_id = rid.slot_id(),
        .bytes = inserted_bytes,
    }};
    auto inserted = adapter.log(
        access::HeapWalMutation{
            .operation = access::HeapWalOperation::kInsert,
            .xid = base::Xid{10},
            .address = kAddress,
            .will_initialize = false,
            .before_page = &before_insert,
            .after_page = &page,
            .tuples = inserted_tuples,
        }
    );
    ASSERT_TRUE(inserted);
    ASSERT_TRUE(page.set_lsn(inserted.value()));
    ASSERT_TRUE(manager->flush_through(inserted.value()));
    ASSERT_TRUE(manager->shutdown());

    auto reader = wal::WalReader::open(
        root->join("wal"),
        wal::WalReaderOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = 1,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    ASSERT_TRUE(reader);
    auto init_record = reader->read_next();
    auto insert_record = reader->read_next();
    ASSERT_TRUE(init_record);
    ASSERT_TRUE(init_record->has_value());
    ASSERT_TRUE(insert_record);
    ASSERT_TRUE(insert_record->has_value());
    EXPECT_FALSE(reader->read_next()->has_value());

    const auto& init_block = init_record->value().record.blocks.front();
    EXPECT_EQ(init_record->value().record.info, wal::opcode::heap::kInitialize);
    EXPECT_TRUE(init_block.will_initialize);
    EXPECT_TRUE(init_block.full_page_image.has_value());
    EXPECT_TRUE(init_block.delta.empty());
    const auto& insert_block = insert_record->value().record.blocks.front();
    EXPECT_EQ(insert_record->value().record.info, wal::opcode::heap::kInsert);
    EXPECT_FALSE(insert_block.full_page_image.has_value());
    ASSERT_FALSE(insert_block.delta.empty());
    EXPECT_EQ(
        decode_heap_wal_payload(wal::opcode::heap::kInsert, insert_block.delta)->tuples.size(),
        1U
    );

    auto restored = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    ASSERT_TRUE(restored.set_lsn(base::Lsn{inserted->value() + 1U}));
    EXPECT_EQ(
        apply_heap_redo(init_record->value(), restored).value(),
        HeapRedoDisposition::kApplied
    );
    EXPECT_EQ(open_heap(restored).item_count(), 0U);
    EXPECT_EQ(
        apply_heap_redo(insert_record->value(), restored).value(),
        HeapRedoDisposition::kApplied
    );
    EXPECT_TRUE(payload_is(open_heap(restored).tuple(rid.slot_id())->payload, "row"));
    const auto once = restored.bytes();
    EXPECT_EQ(
        apply_heap_redo(insert_record->value(), restored).value(),
        HeapRedoDisposition::kSkipped
    );
    EXPECT_EQ(restored.bytes(), once);
}

TEST(HeapWalAdapterTest, RejectsGloballyInvalidAfterPageBeforeWritingWal) {
    auto root = test_support::TestDirectory::create("heap-wal-invalid-after-page");
    ASSERT_TRUE(root);
    auto manager = wal::WalManager::open(root->join("wal"), manager_options());
    ASSERT_TRUE(manager);
    HeapMutationWalAdapter adapter{manager.value()};
    auto page = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    auto heap = open_heap(page);
    const auto first = heap.insert(base::Xid{10}, 0, bytes("first")).value();
    const auto second = heap.insert(base::Xid{11}, 0, bytes("second")).value();
    const auto first_pointer = heap.line_pointer(first.slot_id()).value();
    const auto second_pointer_offset =
        storage::kPageHeaderSize
        + static_cast<std::size_t>(second.slot_id().value()) * access::kLinePointerSize;
    auto second_pointer_bytes =
        page.mutable_view(second_pointer_offset, access::kLinePointerSize).value();
    ASSERT_TRUE(first_pointer.encode(second_pointer_bytes));
    ASSERT_TRUE(access::HeapPage::open(page, kAddress));
    const auto end_lsn_before = manager->end_lsn();

    auto logged = adapter.log(
        access::HeapWalMutation{
            .operation = access::HeapWalOperation::kInitialize,
            .xid = base::kInvalidXid,
            .address = kAddress,
            .will_initialize = true,
            .before_page = nullptr,
            .after_page = &page,
            .tuples = {},
        }
    );

    ASSERT_FALSE(logged);
    EXPECT_EQ(logged.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(manager->end_lsn(), end_lsn_before);
    EXPECT_TRUE(manager->shutdown());
}

TEST(HeapWalRedoTest, AppliesMetadataAndHotUpdatePayloads) {
    auto base_page = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    auto base_heap = open_heap(base_page);
    const auto root = base_heap.insert(base::Xid{10}, 1, bytes("old")).value();
    ASSERT_TRUE(base_page.set_lsn(base::Lsn{80}));

    auto locked = base_heap.tuple(root.slot_id())->header;
    locked.xmax = base::Xid{20};
    locked.flags = access::tuple_flags::kXmaxLockOnly;
    const HeapWalPayload lock_payload{
        .operation = access::HeapWalOperation::kLock,
        .page_flags = 0,
        .tuples = {HeapWalTupleData{.slot = root.slot_id(), .bytes = encoded_header(locked)}},
        .redirects = {},
        .dead = {},
        .unused = {},
    };
    auto metadata_page = base_page;
    EXPECT_EQ(
        apply_heap_redo(
            redo_record(lock_payload, base::Xid{20}, base::Lsn{100}, base::Lsn{120}),
            metadata_page
        )
            .value(),
        HeapRedoDisposition::kApplied
    );
    EXPECT_TRUE(open_heap(metadata_page)
                    .tuple(root.slot_id())
                    ->header.has_flag(access::tuple_flags::kXmaxLockOnly));

    auto updated = locked;
    updated.ctid = base::Rid{base::PageId{7}, base::SlotId{4}};
    updated.flags = access::tuple_flags::kUpdated;
    const HeapWalPayload update_payload{
        .operation = access::HeapWalOperation::kUpdate,
        .page_flags = access::heap_page_flags::kPrunable,
        .tuples = {HeapWalTupleData{.slot = root.slot_id(), .bytes = encoded_header(updated)}},
        .redirects = {},
        .dead = {},
        .unused = {},
    };
    EXPECT_EQ(
        apply_heap_redo(
            redo_record(update_payload, base::Xid{20}, base::Lsn{120}, base::Lsn{140}),
            metadata_page
        )
            .value(),
        HeapRedoDisposition::kApplied
    );
    auto updated_heap = open_heap(metadata_page);
    EXPECT_EQ(updated_heap.tuple(root.slot_id())->header.ctid, updated.ctid);
    EXPECT_EQ(metadata_page.flags().value(), access::heap_page_flags::kPrunable);

    auto deleted = base_heap.tuple(root.slot_id())->header;
    deleted.xmax = base::Xid{30};
    deleted.flags = access::tuple_flags::kDeleted;
    const HeapWalPayload delete_payload{
        .operation = access::HeapWalOperation::kDelete,
        .page_flags = access::heap_page_flags::kPrunable,
        .tuples = {HeapWalTupleData{.slot = root.slot_id(), .bytes = encoded_header(deleted)}},
        .redirects = {},
        .dead = {},
        .unused = {},
    };
    auto deleted_page = base_page;
    ASSERT_TRUE(apply_heap_redo(
        redo_record(delete_payload, base::Xid{30}, base::Lsn{100}, base::Lsn{130}),
        deleted_page
    ));
    EXPECT_TRUE(open_heap(deleted_page)
                    .tuple(root.slot_id())
                    ->header.has_flag(access::tuple_flags::kDeleted));

    auto hot_old = base_heap.tuple(root.slot_id())->header;
    const auto successor = base::Rid{kPageId, base::SlotId{1}};
    hot_old.xmax = base::Xid{40};
    hot_old.ctid = successor;
    hot_old.flags = access::tuple_flags::kUpdated | access::tuple_flags::kHotUpdated;
    auto hot_new = access::TupleHeader::inserted(base::Xid{40}, successor, 2);
    hot_new.flags = access::tuple_flags::kHeapOnly;
    const HeapWalPayload hot_payload{
        .operation = access::HeapWalOperation::kHotUpdate,
        .page_flags = access::heap_page_flags::kPrunable,
        .tuples =
            {
                HeapWalTupleData{.slot = root.slot_id(), .bytes = encoded_header(hot_old)},
                HeapWalTupleData{
                    .slot = successor.slot_id(),
                    .bytes = access::encode_heap_tuple(hot_new, bytes("new")).value(),
                },
            },
        .redirects = {},
        .dead = {},
        .unused = {},
    };
    auto hot_page = base_page;
    ASSERT_TRUE(apply_heap_redo(
        redo_record(hot_payload, base::Xid{40}, base::Lsn{100}, base::Lsn{150}),
        hot_page
    ));
    auto hot_heap = open_heap(hot_page);
    EXPECT_EQ(hot_heap.tuple(root.slot_id())->header.ctid, successor);
    EXPECT_TRUE(payload_is(hot_heap.tuple(successor.slot_id())->payload, "new"));
}

TEST(HeapWalRedoTest, ReplaysCompactPruneAndRepairsFragmentation) {
    auto page = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    auto heap = open_heap(page);
    const auto root = heap.insert(base::Xid{10}, 1, bytes("root")).value();
    auto obsolete_header =
        access::TupleHeader::inserted(base::Xid{20}, base::Rid{kPageId, base::SlotId{1}}, 2);
    obsolete_header.flags = access::tuple_flags::kHeapOnly;
    ASSERT_EQ(heap.insert_tuple(obsolete_header, bytes("obsolete")).value(), base::SlotId{1});
    auto terminal_header =
        access::TupleHeader::inserted(base::Xid{30}, base::Rid{kPageId, base::SlotId{2}}, 3);
    terminal_header.flags = access::tuple_flags::kHeapOnly;
    ASSERT_EQ(heap.insert_tuple(terminal_header, bytes("terminal")).value(), base::SlotId{2});
    const auto victim = heap.insert(base::Xid{11}, 1, bytes("victim")).value();
    ASSERT_TRUE(page.set_lsn(base::Lsn{80}));

    const HeapWalPayload prune_payload{
        .operation = access::HeapWalOperation::kPrune,
        .page_flags = 0,
        .tuples = {},
        .redirects = {access::HeapPruneRedirect{
            .root = root.slot_id(),
            .target = base::SlotId{2},
        }},
        .dead = {victim.slot_id()},
        .unused = {base::SlotId{1}},
    };
    auto encoded = encode_heap_wal_payload(prune_payload);
    ASSERT_TRUE(encoded);
    EXPECT_LT(encoded->size(), 64U);

    EXPECT_EQ(
        apply_heap_redo(
            redo_record(prune_payload, base::kInvalidXid, base::Lsn{100}, base::Lsn{140}),
            page
        )
            .value(),
        HeapRedoDisposition::kApplied
    );
    auto restored = open_heap(page);
    EXPECT_EQ(restored.line_pointer(root.slot_id())->state, access::LinePointerState::kRedirect);
    EXPECT_EQ(restored.resolve(root.slot_id()).value(), base::SlotId{2});
    EXPECT_EQ(restored.line_pointer(base::SlotId{1})->state, access::LinePointerState::kUnused);
    EXPECT_EQ(restored.line_pointer(victim.slot_id())->state, access::LinePointerState::kDead);
    EXPECT_TRUE(payload_is(restored.tuple(base::SlotId{2})->payload, "terminal"));
    EXPECT_TRUE(restored.validate_layout());
}

TEST(HeapWalAdapterTest, DerivesCanonicalCompactPruneChangesFromPageImages) {
    auto root = test_support::TestDirectory::create("heap-wal-prune-adapter");
    ASSERT_TRUE(root);
    auto manager = wal::WalManager::open(root->join("wal"), manager_options());
    ASSERT_TRUE(manager);
    HeapMutationWalAdapter adapter{manager.value()};
    auto page = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    auto heap = open_heap(page);
    const auto source = heap.insert(base::Xid{10}, 1, bytes("source")).value();
    const auto obsolete = heap.insert(base::Xid{20}, 2, bytes("obsolete")).value();
    const auto target = heap.insert(base::Xid{30}, 3, bytes("target")).value();
    ASSERT_TRUE(page.set_lsn(base::Lsn{manager->end_lsn().value() + 1024U}));
    const auto before = page;
    ASSERT_TRUE(heap.redirect(source.slot_id(), target.slot_id()));
    ASSERT_TRUE(heap.mark_dead(obsolete.slot_id()));
    ASSERT_TRUE(heap.mark_unused(obsolete.slot_id()));
    ASSERT_TRUE(heap.repair_fragmentation());

    auto logged = adapter.log(
        access::HeapWalMutation{
            .operation = access::HeapWalOperation::kPrune,
            .xid = base::kInvalidXid,
            .address = kAddress,
            .will_initialize = false,
            .before_page = &before,
            .after_page = &page,
            .tuples = {},
        }
    );
    ASSERT_TRUE(logged);
    ASSERT_TRUE(manager->flush_through(logged.value()));
    ASSERT_TRUE(manager->shutdown());

    auto reader = wal::WalReader::open(
        root->join("wal"),
        wal::WalReaderOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = 1,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    ASSERT_TRUE(reader);
    auto record = reader->read_next();
    ASSERT_TRUE(record);
    ASSERT_TRUE(record->has_value());
    const auto& block = record->value().record.blocks.front();
    EXPECT_FALSE(block.full_page_image.has_value());
    EXPECT_LT(block.delta.size(), 64U);
    auto payload = decode_heap_wal_payload(wal::opcode::heap::kPrune, block.delta);
    ASSERT_TRUE(payload);
    EXPECT_EQ(
        payload->redirects,
        (std::vector{access::HeapPruneRedirect{
            .root = source.slot_id(),
            .target = target.slot_id(),
        }})
    );
    EXPECT_TRUE(payload->dead.empty());
    EXPECT_EQ(payload->unused, (std::vector{obsolete.slot_id()}));
}

TEST(HeapWalAdapterTest, DerivesIndexCleanedDeadSlotAsUnused) {
    auto root = test_support::TestDirectory::create("heap-wal-index-cleanup-adapter");
    ASSERT_TRUE(root);
    auto manager = wal::WalManager::open(root->join("wal"), manager_options());
    ASSERT_TRUE(manager);
    HeapMutationWalAdapter adapter{manager.value()};
    auto page = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    auto heap = open_heap(page);
    ASSERT_TRUE(heap.insert(base::Xid{10}, 1, bytes("live")));
    const auto dead = heap.insert(base::Xid{11}, 2, bytes("dead")).value();
    ASSERT_TRUE(heap.mark_dead(dead.slot_id()));
    ASSERT_TRUE(page.set_lsn(base::Lsn{manager->end_lsn().value() + 1024U}));
    const auto before = page;

    ASSERT_TRUE(heap.mark_unused(dead.slot_id()));
    ASSERT_TRUE(heap.repair_fragmentation());

    auto logged = adapter.log(
        access::HeapWalMutation{
            .operation = access::HeapWalOperation::kPrune,
            .xid = base::kInvalidXid,
            .address = kAddress,
            .will_initialize = false,
            .before_page = &before,
            .after_page = &page,
            .tuples = {},
        }
    );
    ASSERT_TRUE(logged) << logged.error().to_string();
    ASSERT_TRUE(manager->flush_through(logged.value()));
    ASSERT_TRUE(manager->shutdown());

    auto reader = wal::WalReader::open(
        root->join("wal"),
        wal::WalReaderOptions{
            .system_identifier = kSystemIdentifier,
            .timeline = 1,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    ASSERT_TRUE(reader);
    auto record = reader->read_next();
    ASSERT_TRUE(record);
    ASSERT_TRUE(record->has_value());
    const auto& block = record->value().record.blocks.front();
    EXPECT_FALSE(block.full_page_image.has_value());
    auto payload = decode_heap_wal_payload(wal::opcode::heap::kPrune, block.delta);
    ASSERT_TRUE(payload);
    EXPECT_TRUE(payload->redirects.empty());
    EXPECT_TRUE(payload->dead.empty());
    EXPECT_EQ(payload->unused, (std::vector{dead.slot_id()}));
}

TEST(HeapWalRedoTest, FullPageImageRepairsNewerCorruptionAndBadDeltaIsAtomic) {
    auto expected = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    auto expected_heap = open_heap(expected);
    ASSERT_TRUE(expected_heap.insert(base::Xid{10}, 1, bytes("image")));
    wal::WalPageImage image{};
    std::copy(expected.bytes().begin(), expected.bytes().end(), image.begin());
    const wal::DecodedWalRecord image_record{
        .start_lsn = base::Lsn{100},
        .end_lsn = base::Lsn{140},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{10},
            .resource_manager = wal::WalResourceManager::kHeap,
            .info = wal::opcode::heap::kInsert,
            .blocks = {wal::WalBlockReference{
                .relation_id = kAddress.relation_id,
                .page_id = kAddress.page_id,
                .will_initialize = false,
                .standard_page = true,
                .full_page_image = image,
                .delta = {},
            }},
            .main_payload = {},
        },
    };
    auto damaged = expected;
    ASSERT_TRUE(damaged.set_lsn(base::Lsn{999}));
    auto magic = damaged.mutable_view(0, sizeof(std::uint32_t)).value();
    std::fill(magic.begin(), magic.end(), std::byte{0});

    EXPECT_EQ(apply_heap_redo(image_record, damaged).value(), HeapRedoDisposition::kApplied);
    ASSERT_TRUE(expected.set_lsn(image_record.end_lsn));
    EXPECT_EQ(damaged.bytes(), expected.bytes());

    auto malformed = redo_record(
        HeapWalPayload{
            .operation = access::HeapWalOperation::kInsert,
            .page_flags = 0,
            .tuples = {HeapWalTupleData{
                .slot = base::SlotId{1},
                .bytes = tuple(base::SlotId{1}, base::Xid{20}, "later"),
            }},
            .redirects = {},
            .dead = {},
            .unused = {},
        },
        base::Xid{20},
        base::Lsn{140},
        base::Lsn{180}
    );
    malformed.record.blocks.front().delta.pop_back();
    const auto before = damaged.bytes();
    auto failed = apply_heap_redo(malformed, damaged);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(damaged.bytes(), before);

    const auto wrong_kind = storage::Page::create(storage::PageKind::kBtree, kAddress).value();
    wal::WalPageImage wrong_kind_image{};
    std::copy(wrong_kind.bytes().begin(), wrong_kind.bytes().end(), wrong_kind_image.begin());
    auto wrong_kind_record = image_record;
    wrong_kind_record.record.blocks.front().full_page_image = wrong_kind_image;
    const auto before_wrong_kind = damaged.bytes();
    failed = apply_heap_redo(wrong_kind_record, damaged);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(damaged.bytes(), before_wrong_kind);

    auto overlapping = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    auto overlapping_heap = open_heap(overlapping);
    const auto first = overlapping_heap.insert(base::Xid{10}, 1, bytes("first")).value();
    const auto second = overlapping_heap.insert(base::Xid{11}, 1, bytes("second")).value();
    const auto first_pointer = overlapping_heap.line_pointer(first.slot_id()).value();
    const auto second_pointer_offset =
        storage::kPageHeaderSize
        + static_cast<std::size_t>(second.slot_id().value()) * access::kLinePointerSize;
    auto second_pointer_bytes =
        overlapping.mutable_view(second_pointer_offset, access::kLinePointerSize).value();
    ASSERT_TRUE(first_pointer.encode(second_pointer_bytes));
    wal::WalPageImage overlapping_image{};
    std::copy(overlapping.bytes().begin(), overlapping.bytes().end(), overlapping_image.begin());
    auto overlapping_record = image_record;
    overlapping_record.record.blocks.front().full_page_image = overlapping_image;
    const auto before_overlapping = damaged.bytes();
    failed = apply_heap_redo(overlapping_record, damaged);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(damaged.bytes(), before_overlapping);

    const auto expected_header = expected.header();
    ASSERT_TRUE(expected_header);
    ASSERT_GT(expected_header->upper - expected_header->lower, 1U);
    auto mismatched_hole_record = image_record;
    mismatched_hole_record.record.blocks.front().image_hole = wal::WalPageImageHole{
        .offset = static_cast<std::uint16_t>(expected_header->lower + 1U),
        .length = static_cast<std::uint16_t>(expected_header->upper - expected_header->lower - 1U),
    };
    const auto before_mismatched_hole = damaged.bytes();
    failed = apply_heap_redo(mismatched_hole_record, damaged);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(damaged.bytes(), before_mismatched_hole);
}

TEST(HeapWalRedoTest, FullPageImageBaseAppliesItsRetainedDeltaAtomically) {
    auto before = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    const auto inserted = tuple(base::SlotId{0U}, base::Xid{10U}, "image-then-delta");
    auto decoded_tuple = access::decode_heap_tuple(inserted);
    ASSERT_TRUE(decoded_tuple) << decoded_tuple.error().to_string();

    auto expected = before;
    auto expected_heap = open_heap(expected);
    auto expected_slot = expected_heap.insert_tuple(decoded_tuple->header, decoded_tuple->payload);
    ASSERT_TRUE(expected_slot) << expected_slot.error().to_string();
    EXPECT_EQ(expected_slot.value(), base::SlotId{0U});

    wal::WalPageImage image{};
    std::copy(before.bytes().begin(), before.bytes().end(), image.begin());
    const auto record = wal::DecodedWalRecord{
        .start_lsn = base::Lsn{100U},
        .end_lsn = base::Lsn{140U},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{10U},
            .resource_manager = wal::WalResourceManager::kHeap,
            .info = wal::opcode::heap::kInsert,
            .blocks = {wal::WalBlockReference{
                .relation_id = kAddress.relation_id,
                .page_id = kAddress.page_id,
                .will_initialize = false,
                .standard_page = true,
                .full_page_image = image,
                .delta = encode_heap_wal_payload(
                             HeapWalPayload{
                                 .operation = access::HeapWalOperation::kInsert,
                                 .page_flags = 0U,
                                 .tuples = {HeapWalTupleData{
                                     .slot = base::SlotId{0U},
                                     .bytes = inserted,
                                 }},
                                 .redirects = {},
                                 .dead = {},
                                 .unused = {},
                             }
                )
                             .value(),
            }},
            .main_payload = {},
        },
    };

    auto target = before;
    ASSERT_TRUE(target.set_lsn(base::Lsn{999U}));
    auto magic = target.mutable_view(0U, sizeof(std::uint32_t));
    ASSERT_TRUE(magic) << magic.error().to_string();
    std::fill(magic->begin(), magic->end(), std::byte{0});

    auto applied = apply_heap_redo(record, target);
    ASSERT_TRUE(applied) << applied.error().to_string();
    EXPECT_EQ(applied.value(), HeapRedoDisposition::kApplied);
    ASSERT_TRUE(expected.set_lsn(record.end_lsn));
    EXPECT_EQ(target.bytes(), expected.bytes());

    auto malformed = record;
    malformed.record.blocks.front().delta.pop_back();
    const auto published = target.bytes();
    auto rejected = apply_heap_redo(malformed, target);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(target.bytes(), published);
}

TEST(HeapWalRedoTest, FullPageImageDoesNotSuppressALaterDelta) {
    auto before = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    wal::WalPageImage image{};
    std::copy(before.bytes().begin(), before.bytes().end(), image.begin());
    const auto image_record = wal::DecodedWalRecord{
        .start_lsn = base::Lsn{100U},
        .end_lsn = base::Lsn{140U},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{10U},
            .resource_manager = wal::WalResourceManager::kHeap,
            .info = wal::opcode::heap::kInitialize,
            .blocks = {wal::WalBlockReference{
                .relation_id = kAddress.relation_id,
                .page_id = kAddress.page_id,
                .will_initialize = true,
                .standard_page = true,
                .full_page_image = image,
                .delta = {},
            }},
            .main_payload = {},
        },
    };
    const auto delta_record = redo_record(
        HeapWalPayload{
            .operation = access::HeapWalOperation::kInsert,
            .page_flags = 0U,
            .tuples = {HeapWalTupleData{
                .slot = base::SlotId{0U},
                .bytes = tuple(base::SlotId{0U}, base::Xid{10U}, "later-delta"),
            }},
            .redirects = {},
            .dead = {},
            .unused = {},
        },
        base::Xid{10U},
        image_record.end_lsn,
        base::Lsn{180U}
    );

    auto expected = before;
    auto expected_heap = open_heap(expected);
    auto delta_payload =
        decode_heap_wal_payload(delta_record.record.info, delta_record.record.blocks.front().delta);
    ASSERT_TRUE(delta_payload) << delta_payload.error().to_string();
    auto expected_tuple = access::decode_heap_tuple(delta_payload->tuples.front().bytes);
    ASSERT_TRUE(expected_tuple) << expected_tuple.error().to_string();
    auto inserted = expected_heap.insert_tuple(expected_tuple->header, expected_tuple->payload);
    ASSERT_TRUE(inserted) << inserted.error().to_string();
    EXPECT_EQ(inserted.value(), base::SlotId{0U});
    ASSERT_TRUE(expected.set_lsn(delta_record.end_lsn));

    auto target = before;
    ASSERT_TRUE(target.set_lsn(base::Lsn{999U}));
    auto magic = target.mutable_view(0U, sizeof(std::uint32_t));
    ASSERT_TRUE(magic) << magic.error().to_string();
    std::fill(magic->begin(), magic->end(), std::byte{0});

    auto restored = apply_heap_redo(image_record, target);
    ASSERT_TRUE(restored) << restored.error().to_string();
    EXPECT_EQ(restored.value(), HeapRedoDisposition::kApplied);
    EXPECT_EQ(target.lsn(), image_record.end_lsn);

    auto applied = apply_heap_redo(delta_record, target);
    ASSERT_TRUE(applied) << applied.error().to_string();
    EXPECT_EQ(applied.value(), HeapRedoDisposition::kApplied);
    EXPECT_EQ(target.bytes(), expected.bytes());
}

TEST(HeapWalRedoTest, WillInitializeDeltaDoesNotSkipANewerTargetLsn) {
    auto target = storage::Page::create(storage::PageKind::kHeap, kAddress).value();
    ASSERT_TRUE(target.set_lsn(base::Lsn{999U}));
    auto record = redo_record(
        HeapWalPayload{
            .operation = access::HeapWalOperation::kInitialize,
            .page_flags = 0U,
            .tuples = {},
            .redirects = {},
            .dead = {},
            .unused = {},
        },
        base::Xid{30U},
        base::Lsn{100U},
        base::Lsn{140U},
        true
    );

    auto applied = apply_heap_redo(record, target);
    ASSERT_TRUE(applied) << applied.error().to_string();
    EXPECT_EQ(applied.value(), HeapRedoDisposition::kApplied);
    EXPECT_EQ(target.lsn(), record.end_lsn);
    EXPECT_EQ(open_heap(target).item_count(), 0U);
}

}  // namespace
}  // namespace db::recovery
