#include "db/access/heap_page.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "heap_page_scan_view.hpp"

namespace db::access {
namespace {

constexpr std::uint32_t kOffsetMask = 0x7fffU;
constexpr std::uint32_t kStateMask = 0x3U;
constexpr std::uint32_t kStateShift = 15U;
constexpr std::uint32_t kLengthShift = 17U;

constexpr std::size_t kLowerOffset = 16U;

template <std::size_t Extent>
[[maybe_unused]] auto
load_u16_le(const std::span<const std::byte, Extent> bytes, const std::size_t offset) noexcept
    -> std::uint16_t {
    const auto low = std::to_integer<std::uint16_t>(bytes[offset]);
    const auto high = std::to_integer<std::uint16_t>(bytes[offset + 1U]);
    return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
}

template <std::size_t Extent>
[[maybe_unused]] auto
load_u32_le(const std::span<const std::byte, Extent> bytes, const std::size_t offset) noexcept
    -> std::uint32_t {
    const auto first = std::to_integer<std::uint32_t>(bytes[offset]);
    const auto second = std::to_integer<std::uint32_t>(bytes[offset + 1U]);
    const auto third = std::to_integer<std::uint32_t>(bytes[offset + 2U]);
    const auto fourth = std::to_integer<std::uint32_t>(bytes[offset + 3U]);
    return first | (second << 8U) | (third << 16U) | (fourth << 24U);
}

[[maybe_unused]] auto heap_error(
    const base::ErrorCode code,
    const std::string_view operation,
    const std::string_view message
) -> base::Error {
    return base::Error{code, std::string{message}}.with_context("access", std::string{operation});
}

[[maybe_unused]] auto validate_pointer_shape([[maybe_unused]] const LinePointer& pointer)
    -> base::Status {
    // LAB-S01.Q07:R15-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R15"});
    // LAB-S01.Q07:R15-END
}

[[maybe_unused]] auto validate_tuple_header([[maybe_unused]] const TupleHeader& header)
    -> base::Status {
    // LAB-S01.Q07:R16-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R16"});
    // LAB-S01.Q07:R16-END
}

[[maybe_unused]] auto slot_offset(
    [[maybe_unused]] const storage::PageHeader& header,
    [[maybe_unused]] const base::SlotId slot
) -> base::Result<std::size_t> {
    // LAB-S01.Q07:R17-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R17"});
    // LAB-S01.Q07:R17-END
}

[[maybe_unused]] auto read_checked_line_pointer(
    [[maybe_unused]] const storage::Page& page,
    [[maybe_unused]] const storage::PageHeader& header,
    [[maybe_unused]] const base::SlotId slot
) -> base::Result<LinePointer> {
    // LAB-S01.Q07:R18-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R18"});
    // LAB-S01.Q07:R18-END
}

using TupleRange = std::pair<std::size_t, std::size_t>;

[[maybe_unused]] auto validate_heap_header([[maybe_unused]] const storage::PageHeader& header)
    -> base::Status {
    // LAB-S01.Q07:R19-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R19"});
    // LAB-S01.Q07:R19-END
}

[[maybe_unused]] auto validate_item(
    [[maybe_unused]] const storage::Page& page,
    [[maybe_unused]] const storage::PageHeader& header,
    [[maybe_unused]] const LinePointer& pointer,
    [[maybe_unused]] const std::size_t item_count,
    [[maybe_unused]] std::vector<TupleRange>& ranges
) -> base::Status {
    // LAB-S01.Q07:R20-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R20"});
    // LAB-S01.Q07:R20-END
}

[[maybe_unused]] auto validate_disjoint_ranges([[maybe_unused]] std::vector<TupleRange>& ranges)
    -> base::Status {
    // LAB-S01.Q07:R21-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R21"});
    // LAB-S01.Q07:R21-END
}

}  // namespace

auto LinePointer::unused() noexcept -> LinePointer {
    return {};
}

auto LinePointer::normal(const std::uint16_t offset, const std::uint16_t length) noexcept
    -> LinePointer {
    return {.offset = offset, .length = length, .state = LinePointerState::kNormal};
}

auto LinePointer::redirect(const base::SlotId target) noexcept -> LinePointer {
    return {.offset = target.value(), .length = 0, .state = LinePointerState::kRedirect};
}

auto LinePointer::dead() noexcept -> LinePointer {
    return {.offset = 0, .length = 0, .state = LinePointerState::kDead};
}

auto LinePointer::encode(const base::MutableBytes output) const -> base::Status {
    if (output.size() != kLinePointerSize) {
        return base::fail(heap_error(
            base::ErrorCode::kOutOfRange,
            "encode line pointer",
            "line pointer output must be exactly four bytes"
        ));
    }
    auto valid = validate_pointer_shape(*this);
    if (!valid) {
        return valid;
    }
    if (offset > kOffsetMask || length > kOffsetMask) {
        return base::fail(heap_error(
            base::ErrorCode::kOutOfRange,
            "encode line pointer",
            "line pointer offset or length exceeds the packed field"
        ));
    }
    const auto raw = (static_cast<std::uint32_t>(offset) & kOffsetMask)
                     | ((static_cast<std::uint32_t>(state) & kStateMask) << kStateShift)
                     | ((static_cast<std::uint32_t>(length) & kOffsetMask) << kLengthShift);
    base::LittleEndianWriter writer{output};
    return writer.write_u32(raw);
}

auto LinePointer::decode(const base::Bytes input) -> base::Result<LinePointer> {
    if (input.size() != kLinePointerSize) {
        return base::fail(heap_error(
            base::ErrorCode::kCorruption,
            "decode line pointer",
            "line pointer input must be exactly four bytes"
        ));
    }
    base::LittleEndianReader reader{input};
    auto raw = reader.read_u32();
    if (!raw) {
        return base::fail(std::move(raw).error());
    }
    const LinePointer pointer{
        .offset = static_cast<std::uint16_t>(raw.value() & kOffsetMask),
        .length = static_cast<std::uint16_t>((raw.value() >> kLengthShift) & kOffsetMask),
        .state = static_cast<LinePointerState>((raw.value() >> kStateShift) & kStateMask),
    };
    auto valid = validate_pointer_shape(pointer);
    if (!valid) {
        return base::fail(std::move(valid).error());
    }
    return pointer;
}

auto TupleHeader::inserted(
    const base::Xid xmin_value,
    const base::Rid self,
    const std::uint32_t command_id_value
) noexcept -> TupleHeader {
    return {
        .xmin = xmin_value,
        .xmax = base::kInvalidXid,
        .ctid = self,
        .flags = 0,
        .header_size = kTupleHeaderSize,
        .command_id = command_id_value,
    };
}

auto TupleHeader::has_flag(const std::uint16_t flag) const noexcept -> bool {
    return (flags & flag) != 0U;
}

auto TupleHeader::encode(const base::MutableBytes output) const -> base::Status {
    if (output.size() != kTupleHeaderSize) {
        return base::fail(heap_error(
            base::ErrorCode::kOutOfRange,
            "encode tuple header",
            "tuple header output must be exactly 24 bytes"
        ));
    }
    auto valid = validate_tuple_header(*this);
    if (!valid) {
        return valid;
    }
    base::LittleEndianWriter writer{output};
    auto status = writer.write_u32(xmin.value());
    if (status) {
        status = writer.write_u32(xmax.value());
    }
    if (status) {
        status = writer.write_u32(ctid.page_id().value());
    }
    if (status) {
        status = writer.write_u16(ctid.slot_id().value());
    }
    if (status) {
        status = writer.write_u16(flags);
    }
    if (status) {
        status = writer.write_u16(header_size);
    }
    if (status) {
        status = writer.write_u16(0);
    }
    if (status) {
        status = writer.write_u32(command_id);
    }
    return status;
}

auto TupleHeader::decode(const base::Bytes input) -> base::Result<TupleHeader> {
    if (input.size() < kTupleHeaderSize) {
        return base::fail(heap_error(
            base::ErrorCode::kCorruption,
            "decode tuple header",
            "tuple header is truncated"
        ));
    }
    base::LittleEndianReader reader{input.first(kTupleHeaderSize)};
    auto xmin = reader.read_u32();
    auto xmax = reader.read_u32();
    auto page_id = reader.read_u32();
    auto slot_id = reader.read_u16();
    auto flags = reader.read_u16();
    auto header_size = reader.read_u16();
    auto reserved = reader.read_u16();
    auto command_id = reader.read_u32();
    if (!xmin || !xmax || !page_id || !slot_id || !flags || !header_size || !reserved
        || !command_id) {
        return base::fail(heap_error(
            base::ErrorCode::kCorruption,
            "decode tuple header",
            "tuple header fields are truncated"
        ));
    }
    const TupleHeader decoded{
        .xmin = base::Xid{xmin.value()},
        .xmax = base::Xid{xmax.value()},
        .ctid = base::Rid{base::PageId{page_id.value()}, base::SlotId{slot_id.value()}},
        .flags = flags.value(),
        .header_size = header_size.value(),
        .command_id = command_id.value(),
    };
    if (reserved.value() != 0U) {
        return base::fail(heap_error(
            base::ErrorCode::kCorruption,
            "decode tuple header",
            "tuple header reserved field is nonzero"
        ));
    }
    auto valid = validate_tuple_header(decoded);
    if (!valid) {
        return base::fail(
            heap_error(base::ErrorCode::kCorruption, "decode tuple header", valid.error().message())
        );
    }
    return decoded;
}

auto encode_heap_tuple(const TupleHeader& header, const base::Bytes payload)
    -> base::Result<std::vector<std::byte>> {
    if (payload.size() > kMaximumHeapRowSize) {
        return base::fail(heap_error(
            base::ErrorCode::kResourceExhausted,
            "encode heap tuple",
            "heap tuple cannot fit on one page"
        ));
    }
    std::vector<std::byte> encoded(kTupleHeaderSize + payload.size());
    auto status = header.encode(base::MutableBytes{encoded}.first(kTupleHeaderSize));
    if (!status) {
        return base::fail(std::move(status).error());
    }
    std::copy(payload.begin(), payload.end(), encoded.begin() + kTupleHeaderSize);
    return encoded;
}

auto decode_heap_tuple(const base::Bytes tuple) -> base::Result<HeapTupleView> {
    auto header = TupleHeader::decode(tuple);
    if (!header) {
        return base::fail(std::move(header).error());
    }
    if (header->header_size > tuple.size()) {
        return base::fail(heap_error(
            base::ErrorCode::kCorruption,
            "decode heap tuple",
            "tuple header size exceeds tuple bytes"
        ));
    }
    return HeapTupleView{.header = header.value(), .payload = tuple.subspan(header->header_size)};
}

[[maybe_unused]] auto HeapPageScanView::open([[maybe_unused]] const storage::Page& page)
    -> base::Result<HeapPageScanView> {
    // LAB-S01.Q07:R22-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R22"});
    // LAB-S01.Q07:R22-END
}

auto HeapPageScanView::capture_item_count(const storage::Page& page) noexcept -> std::size_t {
    return borrow(page).item_count();
}

auto HeapPageScanView::item_count() const noexcept -> std::size_t {
    const auto lower = load_u16_le(bytes_, kLowerOffset);
    return (static_cast<std::size_t>(lower) - storage::kPageHeaderSize) / kLinePointerSize;
}

auto HeapPageScanView::borrow(const storage::Page& page) noexcept -> HeapPageScanView {
    return borrow(page.bytes());
}

auto HeapPageScanView::borrow(const storage::Page::Storage& image) noexcept -> HeapPageScanView {
    const auto bytes = std::span<const std::byte, storage::kPageSize>{image};
    return HeapPageScanView{bytes};
}

[[maybe_unused]] auto
HeapPageScanView::line_pointer([[maybe_unused]] const base::SlotId slot) const noexcept
    -> LinePointer {
    // LAB-S01.Q07:R23-BEGIN
    base::lab::missing_algorithm("S01.Q07:R23");
    // LAB-S01.Q07:R23-END
}

[[maybe_unused]] auto
HeapPageScanView::tuple([[maybe_unused]] const LinePointer& pointer) const noexcept
    -> std::optional<HeapTupleView> {
    // LAB-S01.Q07:R24-BEGIN
    base::lab::missing_algorithm("S01.Q07:R24");
    // LAB-S01.Q07:R24-END
}

[[maybe_unused]] auto
HeapPageScanView::tuple_if_normal([[maybe_unused]] const base::SlotId slot) const noexcept
    -> std::optional<HeapTupleView> {
    // LAB-S01.Q07:R25-BEGIN
    base::lab::missing_algorithm("S01.Q07:R25");
    // LAB-S01.Q07:R25-END
}

auto HeapPruneResult::has_storage_changes() const noexcept -> bool {
    return !redirects.empty() || !dead.empty() || !unused.empty();
}

HeapPage::HeapPage(storage::Page& page, const storage::PageAddress address) noexcept
    : page_{&page},
      address_{address} {}

auto HeapPage::open(storage::Page& page, const storage::PageAddress address)
    -> base::Result<HeapPage> {
    if (address.relation_id == base::kInvalidRelationId
        || address.page_id == base::kInvalidPageId) {
        return base::fail(heap_error(
            base::ErrorCode::kInvalidArgument,
            "open heap page",
            "heap page address contains an invalid identifier"
        ));
    }
    auto header = page.header();
    if (!header) {
        return base::fail(std::move(header).error());
    }
    HeapPage heap_page{page, address};
    heap_page.validated_header_ = header.value();
    auto valid = validate_heap_header(header.value());
    if (!valid) {
        return base::fail(std::move(valid).error());
    }
    return heap_page;
}

auto HeapPage::page_header() const -> base::Result<storage::PageHeader> {
    return page_->header();
}

auto HeapPage::item_count() const -> std::size_t {
    return (validated_header_.lower - storage::kPageHeaderSize) / kLinePointerSize;
}

[[maybe_unused]] auto HeapPage::free_space() const -> std::size_t {
    // LAB-S01.Q07:R01-BEGIN
    base::lab::missing_algorithm("S01.Q07:R01");
    // LAB-S01.Q07:R01-END
}

auto HeapPage::line_pointer(const base::SlotId slot) const -> base::Result<LinePointer> {
    return read_checked_line_pointer(*page_, validated_header_, slot);
}

auto HeapPage::tuple_bytes(const base::SlotId slot) const -> base::Result<base::Bytes> {
    auto pointer = line_pointer(slot);
    if (!pointer) {
        return base::fail(std::move(pointer).error());
    }
    return tuple_bytes(pointer.value());
}

auto HeapPage::tuple_bytes(const LinePointer& pointer) const -> base::Result<base::Bytes> {
    if (pointer.state != LinePointerState::kNormal) {
        return base::fail(heap_error(
            base::ErrorCode::kNotFound,
            "read heap tuple",
            "heap slot does not contain a normal tuple"
        ));
    }
    const auto begin = static_cast<std::size_t>(pointer.offset);
    const auto length = static_cast<std::size_t>(pointer.length);
    if (begin < validated_header_.upper || begin > validated_header_.special
        || length > validated_header_.special - begin) {
        return base::fail(heap_error(
            base::ErrorCode::kCorruption,
            "read heap tuple",
            "normal line pointer references bytes outside tuple storage"
        ));
    }
    return page_->view(begin, length);
}

auto HeapPage::tuple_if_normal(const base::SlotId slot) const
    -> base::Result<std::optional<HeapTupleView>> {
    auto pointer = line_pointer(slot);
    if (!pointer) {
        return base::fail(std::move(pointer).error());
    }
    if (pointer->state != LinePointerState::kNormal) {
        return std::optional<HeapTupleView>{};
    }
    auto bytes = tuple_bytes(pointer.value());
    if (!bytes) {
        return base::fail(std::move(bytes).error());
    }
    auto tuple = decode_heap_tuple(bytes.value());
    if (!tuple) {
        return base::fail(std::move(tuple).error());
    }
    return std::optional<HeapTupleView>{std::move(tuple).value()};
}

auto HeapPage::mutable_tuple_bytes(const base::SlotId slot) -> base::Result<base::MutableBytes> {
    auto pointer = line_pointer(slot);
    if (!pointer) {
        return base::fail(std::move(pointer).error());
    }
    if (pointer->state != LinePointerState::kNormal) {
        return base::fail(heap_error(
            base::ErrorCode::kNotFound,
            "mutate heap tuple",
            "heap slot does not contain a normal tuple"
        ));
    }
    const auto begin = static_cast<std::size_t>(pointer->offset);
    const auto length = static_cast<std::size_t>(pointer->length);
    if (begin < validated_header_.upper || begin > validated_header_.special
        || length > validated_header_.special - begin) {
        return base::fail(heap_error(
            base::ErrorCode::kCorruption,
            "mutate heap tuple",
            "normal line pointer references bytes outside tuple storage"
        ));
    }
    return page_->mutable_view(begin, length);
}

auto HeapPage::tuple(const base::SlotId slot) const -> base::Result<HeapTupleView> {
    auto bytes = tuple_bytes(slot);
    if (!bytes) {
        return base::fail(std::move(bytes).error());
    }
    return decode_heap_tuple(bytes.value());
}

auto HeapPage::write_page_header(storage::PageHeader header) -> base::Status {
    header.checksum = 0;
    auto bytes = page_->mutable_view(0, storage::kPageHeaderSize);
    if (!bytes) {
        return base::fail(std::move(bytes).error());
    }
    auto encoded = header.encode(bytes.value());
    if (encoded) {
        validated_header_ = header;
    }
    return encoded;
}

[[maybe_unused]] auto HeapPage::find_unused_slot() -> base::Result<std::optional<base::SlotId>> {
    // LAB-S01.Q07:R02-BEGIN
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R02: algorithm not implemented"}
    );
    // LAB-S01.Q07:R02-END
}

[[maybe_unused]] auto HeapPage::write_line_pointer(
    [[maybe_unused]] const base::SlotId slot,
    [[maybe_unused]] const LinePointer& pointer
) -> base::Status {
    // LAB-S01.Q07:R03-BEGIN
    static_cast<void>(slot);
    static_cast<void>(pointer);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R03: algorithm not implemented"}
    );
    // LAB-S01.Q07:R03-END
}

[[maybe_unused]] auto HeapPage::insert_tuple(
    [[maybe_unused]] const TupleHeader& header,
    [[maybe_unused]] const base::Bytes payload
) -> base::Result<base::SlotId> {
    // LAB-S01.Q07:R04-BEGIN
    static_cast<void>(header);
    static_cast<void>(payload);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R04: algorithm not implemented"}
    );
    // LAB-S01.Q07:R04-END
}

[[maybe_unused]] auto HeapPage::insert_tuple_at(
    [[maybe_unused]] const TupleHeader& header,
    [[maybe_unused]] const base::Bytes payload,
    [[maybe_unused]] const std::optional<base::SlotId> reusable
) -> base::Result<base::SlotId> {
    // LAB-S01.Q07:R05-BEGIN
    static_cast<void>(header);
    static_cast<void>(payload);
    static_cast<void>(reusable);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R05: algorithm not implemented"}
    );
    // LAB-S01.Q07:R05-END
}

[[maybe_unused]] auto HeapPage::insert(
    [[maybe_unused]] const base::Xid xmin,
    [[maybe_unused]] const std::uint32_t command_id,
    [[maybe_unused]] const base::Bytes payload
) -> base::Result<base::Rid> {
    // LAB-S01.Q07:R06-BEGIN
    static_cast<void>(xmin);
    static_cast<void>(command_id);
    static_cast<void>(payload);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R06: algorithm not implemented"}
    );
    // LAB-S01.Q07:R06-END
}

[[maybe_unused]] auto HeapPage::mark_dead([[maybe_unused]] const base::SlotId slot)
    -> base::Status {
    // LAB-S01.Q07:R26-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R26"});
    // LAB-S01.Q07:R26-END
}

[[maybe_unused]] auto HeapPage::mark_unused([[maybe_unused]] const base::SlotId slot)
    -> base::Status {
    // LAB-S01.Q07:R27-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R27"});
    // LAB-S01.Q07:R27-END
}

[[maybe_unused]] auto
HeapPage::redirect([[maybe_unused]] const base::SlotId from, [[maybe_unused]] const base::SlotId to)
    -> base::Status {
    // LAB-S01.Q07:R28-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R28"});
    // LAB-S01.Q07:R28-END
}

[[maybe_unused]] auto HeapPage::resolve([[maybe_unused]] base::SlotId slot) const
    -> base::Result<base::SlotId> {
    // LAB-S01.Q07:R29-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R29"});
    // LAB-S01.Q07:R29-END
}

[[maybe_unused]] auto HeapPage::validate_layout() const -> base::Status {
    // LAB-S01.Q07:R30-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R30"});
    // LAB-S01.Q07:R30-END
}

}  // namespace db::access
