#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "db/access/heap_page.hpp"
#include "db/storage/page.hpp"

namespace db::access {

// Read-only heap cursor for a page that has already crossed the storage
// admission boundary. The span borrows its source bytes; the caller must keep
// either the PageReadGuard or the owned Page::Storage that supplied them alive
// for the complete lifetime of this object and every tuple view it returns.
//
// Unlike HeapPage, this cursor is deliberately private to access scans. It
// does not expose mutation or the public fallible Page::view/ByteReader chain.
// BufferPool admission and access-owned mutation already establish the page
// invariants, so this view only decodes fixed little-endian fields. Payload
// spans continue to borrow the same guarded or owned image.
class HeapPageScanView final {
public:
    // Opens a read view after checking only the page admission boundary. The
    // returned view indexes the fixed page image directly; callers retain the
    // read latch for its complete lifetime.
    static auto open(const storage::Page& page) -> base::Result<HeapPageScanView>;

    // Capture the slot horizon once when a scan first pins the page. Inserts
    // that extend the line-pointer array after this point are outside the
    // current page scan, matching the relation horizon established at begin.
    static auto capture_item_count(const storage::Page& page) noexcept -> std::size_t;

    // Borrow an already-admitted page for one PageReadGuard scope. This is a
    // deliberately cheap operation: it does not decode a Page header through
    // the public checked API and it does not walk the line-pointer array.
    static auto borrow(const storage::Page& page) noexcept -> HeapPageScanView;
    // The caller owns an exact page image for the complete view lifetime.
    // This overload lets scans release the frame latch before decoding slots.
    static auto borrow(const storage::Page::Storage& image) noexcept -> HeapPageScanView;

    // The caller supplies slots below item_count(). The returned payload
    // borrows the source image; non-normal slots are skipped.
    auto item_count() const noexcept -> std::size_t;
    auto line_pointer(base::SlotId slot) const noexcept -> LinePointer;
    auto tuple(const LinePointer& pointer) const noexcept -> std::optional<HeapTupleView>;
    auto tuple_if_normal(base::SlotId slot) const noexcept -> std::optional<HeapTupleView>;

private:
    explicit HeapPageScanView(std::span<const std::byte, storage::kPageSize> bytes) noexcept
        : bytes_{bytes} {}

    std::span<const std::byte, storage::kPageSize> bytes_;
};

}  // namespace db::access
