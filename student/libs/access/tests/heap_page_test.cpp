#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/heap_page.hpp"
#include "db/base/error.hpp"
#include "db/storage/page.hpp"
#include "db/transaction/commit_log.hpp"

#include "heap_page_scan_view.hpp"

namespace db::access {
namespace {

constexpr storage::PageAddress kAddress{
    .relation_id = base::RelationId{41},
    .page_id = base::PageId{7},
};

auto bytes_of(const std::span<const std::uint8_t> bytes) -> base::Bytes {
    return std::as_bytes(bytes);
}

auto equal_bytes(const base::Bytes left, const base::Bytes right) -> bool {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
}

auto make_heap_page() -> storage::Page {
    auto page = storage::Page::create(storage::PageKind::kHeap, kAddress);
    EXPECT_TRUE(page) << page.error().to_string();
    return std::move(page).value();
}

TEST(LinePointerTest, PackedLayoutRoundTripsAndRejectsNoncanonicalStates) {
    const auto pointer = LinePointer::normal(0x0123, 0x0045);
    std::array<std::byte, kLinePointerSize> encoded{};
    ASSERT_TRUE(pointer.encode(encoded));

    constexpr std::uint32_t k_expected_raw = 0x0123U | (1U << 15U) | (0x0045U << 17U);
    EXPECT_EQ(encoded[0], static_cast<std::byte>(k_expected_raw & 0xffU));
    EXPECT_EQ(encoded[1], static_cast<std::byte>((k_expected_raw >> 8U) & 0xffU));
    EXPECT_EQ(LinePointer::decode(encoded).value(), pointer);

    std::array<std::byte, kLinePointerSize>
        corrupt{std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
    const auto decoded = LinePointer::decode(corrupt);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code(), base::ErrorCode::kCorruption);
}

TEST(TupleHeaderTest, StableLittleEndianLayoutAndPayloadBoundary) {
    const TupleHeader header{
        .xmin = base::Xid{0x0102'0304U},
        .xmax = base::Xid{0x1112'1314U},
        .ctid = base::Rid{base::PageId{0x2122'2324U}, base::SlotId{0x3132U}},
        .flags = tuple_flags::kUpdated,
        .header_size = kTupleHeaderSize,
        .command_id = 0x5152'5354U,
    };
    std::array<std::byte, kTupleHeaderSize> encoded{};
    ASSERT_TRUE(header.encode(encoded));
    EXPECT_EQ(std::to_integer<std::uint8_t>(encoded[0]), 0x04U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(encoded[8]), 0x24U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(encoded[12]), 0x32U);
    EXPECT_EQ(std::to_integer<std::uint8_t>(encoded[16]), kTupleHeaderSize);
    EXPECT_EQ(std::to_integer<std::uint8_t>(encoded[20]), 0x54U);
    EXPECT_EQ(TupleHeader::decode(encoded).value(), header);

    const std::array<std::uint8_t, 3> payload{9, 8, 7};
    auto tuple = encode_heap_tuple(header, bytes_of(payload));
    ASSERT_TRUE(tuple);
    auto decoded = decode_heap_tuple(tuple.value());
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->header, header);
    EXPECT_TRUE(equal_bytes(decoded->payload, bytes_of(payload)));
}

TEST(TupleHeaderTest, RejectsReservedFlagsAndTruncatedHeader) {
    const auto header =
        TupleHeader::inserted(base::Xid{1}, base::Rid{base::PageId{1}, base::SlotId{0}}, 0);
    std::array<std::byte, kTupleHeaderSize> encoded{};
    ASSERT_TRUE(header.encode(encoded));
    encoded[18] = std::byte{1};
    EXPECT_EQ(TupleHeader::decode(encoded).error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(
        TupleHeader::decode(base::Bytes{encoded}.first(kTupleHeaderSize - 1U)).error().code(),
        base::ErrorCode::kCorruption
    );
}

TEST(HeapPageTest, InsertsAndReadsSelfIdentifyingTuples) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress);
    ASSERT_TRUE(heap);
    const auto initial_space = heap->free_space();
    const std::array<std::uint8_t, 4> payload{1, 2, 3, 4};

    auto rid = heap->insert(base::Xid{10}, 6, bytes_of(payload));
    ASSERT_TRUE(rid);
    EXPECT_EQ(rid.value(), (base::Rid{kAddress.page_id, base::SlotId{0}}));
    EXPECT_EQ(heap->item_count(), 1U);
    EXPECT_EQ(
        heap->free_space(),
        initial_space - kLinePointerSize - kTupleHeaderSize - payload.size()
    );

    auto tuple = heap->tuple(rid->slot_id());
    ASSERT_TRUE(tuple);
    EXPECT_EQ(tuple->header.xmin, base::Xid{10});
    EXPECT_EQ(tuple->header.ctid, rid.value());
    EXPECT_EQ(tuple->header.command_id, 6U);
    EXPECT_TRUE(equal_bytes(tuple->payload, bytes_of(payload)));
    auto normal = heap->tuple_if_normal(rid->slot_id());
    ASSERT_TRUE(normal);
    ASSERT_TRUE(normal->has_value());
    EXPECT_EQ(normal->value().header, tuple->header);
    EXPECT_TRUE(equal_bytes(normal->value().payload, tuple->payload));
    EXPECT_TRUE(heap->validate_layout());
}

TEST(HeapPageTest, OpenDefersWholePageOverlapAudit) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 2> payload{1, 2};
    const auto first = heap.insert(base::Xid{10}, 0, bytes_of(payload)).value();
    const auto second = heap.insert(base::Xid{11}, 0, bytes_of(payload)).value();
    const auto first_pointer = heap.line_pointer(first.slot_id()).value();
    const auto second_pointer_offset =
        storage::kPageHeaderSize
        + static_cast<std::size_t>(second.slot_id().value()) * kLinePointerSize;
    auto second_pointer_bytes = page.mutable_view(second_pointer_offset, kLinePointerSize).value();
    ASSERT_TRUE(first_pointer.encode(second_pointer_bytes));

    auto reopened = HeapPage::open(page, kAddress);

    ASSERT_TRUE(reopened);
    EXPECT_TRUE(reopened->tuple(second.slot_id()));
    auto layout = reopened->validate_layout();
    ASSERT_FALSE(layout);
    EXPECT_EQ(layout.error().code(), base::ErrorCode::kCorruption);
    const auto before_repair = page.bytes();
    auto repaired = reopened->repair_fragmentation();
    ASSERT_FALSE(repaired);
    EXPECT_EQ(repaired.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(page.bytes(), before_repair);
}

TEST(HeapPageTest, TupleAccessRejectsOnlyTheCorruptRangeItTouches) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 2> payload{1, 2};
    const auto first = heap.insert(base::Xid{10}, 0, bytes_of(payload)).value();
    const auto second = heap.insert(base::Xid{11}, 0, bytes_of(payload)).value();
    const auto first_pointer_offset =
        storage::kPageHeaderSize
        + static_cast<std::size_t>(first.slot_id().value()) * kLinePointerSize;
    auto first_pointer_bytes = page.mutable_view(first_pointer_offset, kLinePointerSize).value();
    ASSERT_TRUE(
        LinePointer::normal(storage::kPageHeaderSize, kTupleHeaderSize).encode(first_pointer_bytes)
    );

    auto reopened = HeapPage::open(page, kAddress);

    ASSERT_TRUE(reopened);
    auto corrupt_tuple = reopened->tuple(first.slot_id());
    ASSERT_FALSE(corrupt_tuple);
    EXPECT_EQ(corrupt_tuple.error().code(), base::ErrorCode::kCorruption);
    EXPECT_TRUE(reopened->tuple(second.slot_id()));
}

TEST(HeapPageTest, DeadSlotsWaitForIndexCleanupBeforeReuse) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 1> payload{5};

    const auto first = heap.insert(base::Xid{1}, 0, bytes_of(payload)).value();
    const auto second = heap.insert(base::Xid{2}, 0, bytes_of(payload)).value();
    ASSERT_TRUE(heap.mark_dead(first.slot_id()));
    const auto third = heap.insert(base::Xid{3}, 0, bytes_of(payload)).value();
    EXPECT_EQ(third.slot_id(), base::SlotId{2});

    ASSERT_TRUE(heap.mark_unused(first.slot_id()));
    const auto reused = heap.insert(base::Xid{4}, 0, bytes_of(payload)).value();
    EXPECT_EQ(reused.slot_id(), first.slot_id());
    EXPECT_EQ(heap.item_count(), 3U);
    EXPECT_EQ(heap.tuple(second.slot_id())->header.xmin, base::Xid{2});
    EXPECT_TRUE(heap.validate_layout());
}

TEST(HeapPageTest, RedirectsResolveOnlyToNormalSlots) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 1> payload{0xaa};
    const auto root = heap.insert(base::Xid{1}, 0, bytes_of(payload)).value();
    const auto successor = heap.insert(base::Xid{2}, 0, bytes_of(payload)).value();

    ASSERT_TRUE(heap.redirect(root.slot_id(), successor.slot_id()));
    EXPECT_EQ(heap.resolve(root.slot_id()).value(), successor.slot_id());
    EXPECT_EQ(heap.line_pointer(root.slot_id())->state, LinePointerState::kRedirect);
    auto redirected = heap.tuple_if_normal(root.slot_id());
    ASSERT_TRUE(redirected);
    EXPECT_FALSE(redirected->has_value());
    auto normal = heap.tuple_if_normal(successor.slot_id());
    ASSERT_TRUE(normal);
    EXPECT_TRUE(normal->has_value());
    EXPECT_TRUE(heap.validate_layout());
    EXPECT_FALSE(heap.redirect(successor.slot_id(), root.slot_id()));
}

TEST(HeapPageTest, LayoutAuditChecksRedirectsAfterNormalAndDeadSlots) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 1> payload{0xaa};
    const auto target = heap.insert(base::Xid{1}, 0, bytes_of(payload)).value();
    const auto dead = heap.insert(base::Xid{2}, 0, bytes_of(payload)).value();
    const auto root = heap.insert(base::Xid{3}, 0, bytes_of(payload)).value();
    ASSERT_TRUE(heap.mark_dead(dead.slot_id()));
    ASSERT_TRUE(heap.redirect(root.slot_id(), target.slot_id()));
    ASSERT_TRUE(heap.validate_layout());

    const auto offset = storage::kPageHeaderSize
                        + static_cast<std::size_t>(root.slot_id().value()) * kLinePointerSize;
    auto bytes = page.mutable_view(offset, kLinePointerSize).value();
    for (const auto invalid_target : {dead.slot_id(), root.slot_id()}) {
        ASSERT_TRUE(LinePointer::redirect(invalid_target).encode(bytes));
        auto reopened = HeapPage::open(page, kAddress);
        ASSERT_TRUE(reopened);
        const auto audited = reopened->validate_layout();
        ASSERT_FALSE(audited);
        EXPECT_EQ(audited.error().code(), base::ErrorCode::kCorruption);
    }
}

TEST(HeapPageTest, FragmentationRepairPreservesRidsAndCompactsTupleStorage) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 5> first_payload{1, 2, 3, 4, 5};
    const std::array<std::uint8_t, 8> removed_payload{8, 7, 6, 5, 4, 3, 2, 1};
    const std::array<std::uint8_t, 3> third_payload{9, 8, 7};
    const auto first = heap.insert(base::Xid{1}, 0, bytes_of(first_payload)).value();
    const auto removed = heap.insert(base::Xid{2}, 0, bytes_of(removed_payload)).value();
    const auto third = heap.insert(base::Xid{3}, 0, bytes_of(third_payload)).value();
    const auto free_before = heap.free_space();
    ASSERT_TRUE(heap.mark_dead(removed.slot_id()));
    ASSERT_TRUE(heap.mark_unused(removed.slot_id()));

    auto reclaimed = heap.repair_fragmentation();

    ASSERT_TRUE(reclaimed);
    EXPECT_EQ(reclaimed.value(), kTupleHeaderSize + removed_payload.size());
    EXPECT_EQ(heap.item_count(), 3U);
    EXPECT_EQ(heap.line_pointer(removed.slot_id())->state, LinePointerState::kUnused);
    EXPECT_TRUE(equal_bytes(heap.tuple(first.slot_id())->payload, bytes_of(first_payload)));
    EXPECT_TRUE(equal_bytes(heap.tuple(third.slot_id())->payload, bytes_of(third_payload)));
    EXPECT_EQ(heap.free_space(), free_before + reclaimed.value());
    EXPECT_TRUE(heap.validate_layout());
}

TEST(HeapPageTest, FragmentationRepairPreservesRedirectAndTruncatesUnusedTail) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 4> root_payload{1, 1, 1, 1};
    const std::array<std::uint8_t, 6> target_payload{2, 2, 2, 2, 2, 2};
    const std::array<std::uint8_t, 7> tail_payload{3, 3, 3, 3, 3, 3, 3};
    const auto root = heap.insert(base::Xid{1}, 0, bytes_of(root_payload)).value();
    const auto target = heap.insert(base::Xid{2}, 0, bytes_of(target_payload)).value();
    const auto tail = heap.insert(base::Xid{3}, 0, bytes_of(tail_payload)).value();
    const auto free_before = heap.free_space();
    ASSERT_TRUE(heap.redirect(root.slot_id(), target.slot_id()));
    ASSERT_TRUE(heap.mark_dead(tail.slot_id()));
    ASSERT_TRUE(heap.mark_unused(tail.slot_id()));

    auto reclaimed = heap.repair_fragmentation();

    ASSERT_TRUE(reclaimed);
    EXPECT_EQ(heap.item_count(), 2U);
    EXPECT_EQ(heap.line_pointer(root.slot_id())->state, LinePointerState::kRedirect);
    EXPECT_EQ(heap.resolve(root.slot_id()).value(), target.slot_id());
    EXPECT_TRUE(equal_bytes(heap.tuple(target.slot_id())->payload, bytes_of(target_payload)));
    EXPECT_EQ(
        reclaimed.value(),
        2U * kTupleHeaderSize + root_payload.size() + tail_payload.size() + kLinePointerSize
    );
    EXPECT_EQ(heap.free_space(), free_before + reclaimed.value());
    EXPECT_TRUE(heap.validate_layout());
}

TEST(HeapPageTest, PruneHorizonProtectsHotRootUntilUpdaterIsGloballyRemovable) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 3> payload{1, 2, 3};
    const auto root = heap.insert(base::Xid{10}, 0, bytes_of(payload)).value();
    const auto successor = heap.insert(base::Xid{20}, 1, bytes_of(payload)).value();
    auto successor_bytes = heap.mutable_tuple_bytes(successor.slot_id()).value();
    auto successor_header = TupleHeader::decode(successor_bytes.first(kTupleHeaderSize)).value();
    successor_header.flags |= tuple_flags::kHeapOnly;
    ASSERT_TRUE(successor_header.encode(successor_bytes.first(kTupleHeaderSize)));
    auto root_bytes = heap.mutable_tuple_bytes(root.slot_id()).value();
    auto root_header = TupleHeader::decode(root_bytes.first(kTupleHeaderSize)).value();
    root_header.xmax = base::Xid{20};
    root_header.ctid = successor;
    root_header.flags |= tuple_flags::kUpdated | tuple_flags::kHotUpdated;
    ASSERT_TRUE(root_header.encode(root_bytes.first(kTupleHeaderSize)));
    ASSERT_TRUE(page.set_flags(heap_page_flags::kPrunable));
    transaction::CommitLog commit_log;
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    ASSERT_TRUE(commit_log.set_status(base::Xid{10}, transaction::TransactionStatus::kCommitted));
    ASSERT_TRUE(commit_log.set_status(base::Xid{20}, transaction::TransactionStatus::kCommitted));

    auto protected_result = heap.prune(commit_log, base::Xid{20});
    ASSERT_TRUE(protected_result);
    EXPECT_FALSE(protected_result->has_storage_changes());
    EXPECT_EQ(heap.line_pointer(root.slot_id())->state, LinePointerState::kNormal);

    auto removable_result = heap.prune(commit_log, base::Xid{21});
    ASSERT_TRUE(removable_result);
    EXPECT_EQ(
        removable_result->redirects,
        (std::vector{HeapPruneRedirect{
            .root = root.slot_id(),
            .target = successor.slot_id(),
        }})
    );
    EXPECT_EQ(heap.line_pointer(root.slot_id())->state, LinePointerState::kRedirect);
    EXPECT_EQ(heap.resolve(root.slot_id()).value(), successor.slot_id());
}

TEST(HeapPageTest, PruneRejectsCrossPageHotLinkWithoutMutatingThePage) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 3> payload{1, 2, 3};
    const auto root = heap.insert(base::Xid{10}, 0, bytes_of(payload)).value();
    auto root_bytes = heap.mutable_tuple_bytes(root.slot_id()).value();
    auto root_header = TupleHeader::decode(root_bytes.first(kTupleHeaderSize)).value();
    root_header.xmax = base::Xid{20};
    root_header.ctid = base::Rid{base::PageId{kAddress.page_id.value() + 1U}, base::SlotId{0}};
    root_header.flags |= tuple_flags::kUpdated | tuple_flags::kHotUpdated;
    ASSERT_TRUE(root_header.encode(root_bytes.first(kTupleHeaderSize)));
    ASSERT_TRUE(page.set_flags(heap_page_flags::kPrunable));
    transaction::CommitLog commit_log;
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    ASSERT_TRUE(commit_log.set_status(base::Xid{10}, transaction::TransactionStatus::kCommitted));
    ASSERT_TRUE(commit_log.set_status(base::Xid{20}, transaction::TransactionStatus::kCommitted));
    const auto before = page.bytes();

    auto rejected = heap.prune(commit_log, base::Xid{21});

    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(page.bytes(), before);
}

TEST(HeapPageTest, PruneReclaimsSequentialAbortedHeapOnlySuccessors) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const auto root = heap.insert(base::Xid{10}, 0, bytes_of(std::array{std::uint8_t{1}})).value();
    const auto first = heap.insert(base::Xid{20}, 1, bytes_of(std::array{std::uint8_t{2}})).value();
    const auto second =
        heap.insert(base::Xid{20}, 2, bytes_of(std::array{std::uint8_t{3}})).value();
    auto first_bytes = heap.mutable_tuple_bytes(first.slot_id()).value();
    auto first_header = TupleHeader::decode(first_bytes.first(kTupleHeaderSize)).value();
    first_header.xmax = base::Xid{20};
    first_header.ctid = second;
    first_header.flags = tuple_flags::kHeapOnly | tuple_flags::kUpdated | tuple_flags::kHotUpdated;
    ASSERT_TRUE(first_header.encode(first_bytes.first(kTupleHeaderSize)));
    auto second_bytes = heap.mutable_tuple_bytes(second.slot_id()).value();
    auto second_header = TupleHeader::decode(second_bytes.first(kTupleHeaderSize)).value();
    second_header.flags = tuple_flags::kHeapOnly;
    ASSERT_TRUE(second_header.encode(second_bytes.first(kTupleHeaderSize)));
    auto root_bytes = heap.mutable_tuple_bytes(root.slot_id()).value();
    auto root_header = TupleHeader::decode(root_bytes.first(kTupleHeaderSize)).value();
    root_header.xmax = base::Xid{20};
    root_header.ctid = first;
    root_header.flags = tuple_flags::kUpdated | tuple_flags::kHotUpdated;
    ASSERT_TRUE(root_header.encode(root_bytes.first(kTupleHeaderSize)));
    ASSERT_TRUE(page.set_flags(heap_page_flags::kPrunable));
    transaction::CommitLog commit_log;
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    ASSERT_TRUE(commit_log.set_status(base::Xid{10}, transaction::TransactionStatus::kCommitted));
    ASSERT_TRUE(commit_log.set_status(base::Xid{20}, transaction::TransactionStatus::kAborted));

    auto pruned = heap.prune(commit_log, base::Xid{21});

    ASSERT_TRUE(pruned);
    EXPECT_EQ(pruned->unused, (std::vector{first.slot_id(), second.slot_id()}));
    EXPECT_EQ(pruned->tuples_pruned, 2U);
    EXPECT_EQ(heap.item_count(), 1U);
    EXPECT_EQ(heap.line_pointer(root.slot_id())->state, LinePointerState::kNormal);
}

TEST(HeapPageTest, RejectsInsufficientSpaceAndWrongPageKind) {
    auto heap_storage = make_heap_page();
    auto heap = HeapPage::open(heap_storage, kAddress).value();
    std::vector<std::byte> oversized(storage::kPageSize, std::byte{0});
    EXPECT_EQ(
        heap.insert(base::Xid{1}, 0, oversized).error().code(),
        base::ErrorCode::kResourceExhausted
    );

    auto btree = storage::Page::create(storage::PageKind::kBtree, kAddress).value();
    EXPECT_EQ(HeapPage::open(btree, kAddress).error().code(), base::ErrorCode::kCorruption);
}

TEST(HeapPageScanViewTest, AdmitsHeapPageOnceAndRejectsWrongPageKind) {
    auto page = make_heap_page();
    auto opened = HeapPageScanView::open(page);
    ASSERT_TRUE(opened);
    EXPECT_EQ(opened->item_count(), 0U);

    auto btree = storage::Page::create(storage::PageKind::kBtree, kAddress).value();
    auto rejected = HeapPageScanView::open(btree);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
}

TEST(HeapPageScanViewTest, DecodesTrustedTupleHeaderAndBorrowsPayloadBytes) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 5> payload{9, 8, 7, 6, 5};
    const auto rid = heap.insert(base::Xid{17}, 23, bytes_of(payload)).value();

    const auto item_count = HeapPageScanView::capture_item_count(page);
    EXPECT_EQ(item_count, heap.item_count());
    const auto borrowed = HeapPageScanView::borrow(page);
    const auto scanned = borrowed.tuple_if_normal(rid.slot_id());
    ASSERT_TRUE(scanned.has_value());

    const auto checked = heap.tuple(rid.slot_id());
    ASSERT_TRUE(checked);
    EXPECT_EQ(scanned->header, checked->header);
    EXPECT_TRUE(equal_bytes(scanned->payload, checked->payload));
    EXPECT_EQ(
        scanned->payload.data(),
        std::span{page.bytes()}
            .subspan(heap.line_pointer(rid.slot_id())->offset + kTupleHeaderSize)
            .data()
    );

    const auto image = page.bytes();
    const auto snapshotted = HeapPageScanView::borrow(image).tuple_if_normal(rid.slot_id());
    ASSERT_TRUE(snapshotted.has_value());
    EXPECT_EQ(snapshotted->header, checked->header);
    EXPECT_TRUE(equal_bytes(snapshotted->payload, checked->payload));
    EXPECT_EQ(
        snapshotted->payload.data(),
        std::span{image}.subspan(heap.line_pointer(rid.slot_id())->offset + kTupleHeaderSize).data()
    );
}

TEST(HeapPageScanViewTest, SkipsEveryNonNormalLinePointerState) {
    auto page = make_heap_page();
    auto heap = HeapPage::open(page, kAddress).value();
    const std::array<std::uint8_t, 1> payload{0x42};
    const auto redirect_root = heap.insert(base::Xid{1}, 0, bytes_of(payload)).value();
    const auto normal_target = heap.insert(base::Xid{2}, 0, bytes_of(payload)).value();
    const auto dead = heap.insert(base::Xid{3}, 0, bytes_of(payload)).value();
    const auto unused = heap.insert(base::Xid{4}, 0, bytes_of(payload)).value();
    ASSERT_TRUE(heap.redirect(redirect_root.slot_id(), normal_target.slot_id()));
    ASSERT_TRUE(heap.mark_dead(dead.slot_id()));
    ASSERT_TRUE(heap.mark_dead(unused.slot_id()));
    ASSERT_TRUE(heap.mark_unused(unused.slot_id()));

    const auto borrowed = HeapPageScanView::borrow(page);
    for (const auto slot : {redirect_root.slot_id(), dead.slot_id(), unused.slot_id()}) {
        const auto scanned = borrowed.tuple_if_normal(slot);
        EXPECT_FALSE(scanned.has_value());
    }
    const auto scanned_target = borrowed.tuple_if_normal(normal_target.slot_id());
    EXPECT_TRUE(scanned_target.has_value());
}

}  // namespace
}  // namespace db::access
