#include "db/access/btree_page.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "btree_page_builder.hpp"

namespace db::access {

namespace {

constexpr std::uint16_t kBtreePageLeafFlag = 1U << 0U;
constexpr std::uint16_t kBtreePageRootFlag = 1U << 1U;
constexpr std::uint16_t kBtreePageIncompleteSplitFlag = 1U << 2U;

static_assert(48U == kBtreeMetapageHeaderSize);
static_assert(32U == kBtreePageOpaqueSize);
static_assert(10U == kBtreeTupleHeaderSize);

struct StoredBtreeMeta final {
    base::PageId root_page_id{base::kInvalidPageId};
    std::uint16_t root_level{0};
    std::uint64_t type_signature{0};
    std::uint16_t column_count{0};
    std::uint16_t maximum_key_size{0};
    std::uint64_t root_generation{0};
};

struct BtreeOpaque final {
    base::PageId previous{base::kInvalidPageId};
    base::PageId next{base::kInvalidPageId};
    std::uint16_t level{0};
    std::uint16_t flags{0};
    std::uint64_t generation{0};
};

struct TupleInput final {
    BtreeTupleKind kind{BtreeTupleKind::kHighKey};
    base::Bytes key;
    base::PageId target_page_id{base::kInvalidPageId};
    base::SlotId target_slot_id{base::kInvalidSlotId};
};

[[maybe_unused]] auto load_u16_le(const base::Bytes bytes, const std::size_t offset) noexcept
    -> std::uint16_t {
    const auto low = std::to_integer<std::uint16_t>(bytes[offset]);
    const auto high = std::to_integer<std::uint16_t>(bytes[offset + 1U]);
    return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
}

[[maybe_unused]] auto load_u32_le(const base::Bytes bytes, const std::size_t offset) noexcept
    -> std::uint32_t {
    const auto first = std::to_integer<std::uint32_t>(bytes[offset]);
    const auto second = std::to_integer<std::uint32_t>(bytes[offset + 1U]);
    const auto third = std::to_integer<std::uint32_t>(bytes[offset + 2U]);
    const auto fourth = std::to_integer<std::uint32_t>(bytes[offset + 3U]);
    return first | (second << 8U) | (third << 16U) | (fourth << 24U);
}

[[maybe_unused]] auto load_u64_le(const base::Bytes bytes, const std::size_t offset) noexcept
    -> std::uint64_t {
    const auto low = static_cast<std::uint64_t>(load_u32_le(bytes, offset));
    const auto high = static_cast<std::uint64_t>(load_u32_le(bytes, offset + 4U));
    return low | (high << 32U);
}

[[maybe_unused]] void store_u16_le(
    const base::MutableBytes bytes,
    const std::size_t offset,
    const std::uint16_t value
) noexcept {
    bytes[offset] = static_cast<std::byte>(static_cast<std::uint8_t>(value));
    bytes[offset + 1U] = static_cast<std::byte>(static_cast<std::uint8_t>(value >> 8U));
}

[[maybe_unused]] void store_u32_le(
    const base::MutableBytes bytes,
    const std::size_t offset,
    const std::uint32_t value
) noexcept {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

[[maybe_unused]] void store_u64_le(
    const base::MutableBytes bytes,
    const std::size_t offset,
    const std::uint64_t value
) noexcept {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

[[maybe_unused]] auto mutable_page_bytes(storage::Page& page) noexcept -> base::MutableBytes {
    return page.mutable_view().bytes();
}

[[maybe_unused]] auto resident_storage_header(const storage::Page& page) noexcept
    -> storage::PageHeader {
    const auto bytes = base::Bytes{page.bytes()};
    return storage::PageHeader{
        .magic = load_u32_le(bytes, 0U),
        .version = load_u16_le(bytes, 4U),
        .header_size = load_u16_le(bytes, 6U),
        .checksum = load_u32_le(bytes, 8U),
        .flags = load_u16_le(bytes, 12U),
        .kind = static_cast<storage::PageKind>(load_u16_le(bytes, 14U)),
        .lower = load_u16_le(bytes, 16U),
        .upper = load_u16_le(bytes, 18U),
        .special = load_u16_le(bytes, 20U),
        .reserved = load_u16_le(bytes, 22U),
        .lsn = base::Lsn{load_u64_le(bytes, 24U)},
    };
}

[[maybe_unused]] auto resident_opaque(const storage::Page& page) noexcept -> BtreeOpaque {
    constexpr std::size_t offset = storage::kPageSize - kBtreePageOpaqueSize;
    const auto bytes = base::Bytes{page.bytes()};
    return BtreeOpaque{
        .previous = base::PageId{load_u32_le(bytes, offset + 8U)},
        .next = base::PageId{load_u32_le(bytes, offset + 12U)},
        .level = load_u16_le(bytes, offset + 16U),
        .flags = load_u16_le(bytes, offset + 18U),
        .generation = load_u64_le(bytes, offset + 24U),
    };
}

[[maybe_unused]] auto resident_meta(const storage::Page& page) noexcept -> StoredBtreeMeta {
    constexpr std::size_t offset = storage::kPageHeaderSize;
    const auto bytes = base::Bytes{page.bytes()};
    return StoredBtreeMeta{
        .root_page_id = base::PageId{load_u32_le(bytes, offset + 12U)},
        .root_level = load_u16_le(bytes, offset + 16U),
        .type_signature = load_u64_le(bytes, offset + 20U),
        .column_count = load_u16_le(bytes, offset + 28U),
        .maximum_key_size = load_u16_le(bytes, offset + 30U),
        .root_generation = load_u64_le(bytes, offset + 32U),
    };
}

[[maybe_unused]] auto btree_error(
    const base::ErrorCode code,
    const std::string_view operation,
    const std::string_view detail
) -> base::Error {
    return {code, "B-tree " + std::string{operation} + ": " + std::string{detail}};
}

[[maybe_unused]] void write_storage_header(
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] storage::PageHeader header
) noexcept {
    // LAB-S03.Q01:R36-BEGIN
    base::lab::missing_algorithm("S03.Q01:R36");
    // LAB-S03.Q01:R36-END
}

[[maybe_unused]] void reset_storage_page(
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const storage::PageKind expected_kind,
    [[maybe_unused]] const std::uint16_t special
) noexcept {
    // LAB-S03.Q01:R37-BEGIN
    base::lab::missing_algorithm("S03.Q01:R37");
    // LAB-S03.Q01:R37-END
}

[[maybe_unused]] void write_meta(
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const StoredBtreeMeta metadata
) noexcept {
    // LAB-S03.Q01:R38-BEGIN
    base::lab::missing_algorithm("S03.Q01:R38");
    // LAB-S03.Q01:R38-END
}

[[maybe_unused]] void write_opaque(
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const BtreeOpaque opaque
) noexcept {
    // LAB-S03.Q01:R39-BEGIN
    base::lab::missing_algorithm("S03.Q01:R39");
    // LAB-S03.Q01:R39-END
}

[[maybe_unused]] void advance_generation([[maybe_unused]] std::uint64_t& generation) noexcept {
    // LAB-S03.Q01:R40-BEGIN
    base::lab::missing_algorithm("S03.Q01:R40");
    // LAB-S03.Q01:R40-END
}

[[maybe_unused]] auto tuple_size([[maybe_unused]] const TupleInput input) noexcept -> std::size_t {
    // LAB-S03.Q01:R41-BEGIN
    base::lab::missing_algorithm("S03.Q01:R41");
    // LAB-S03.Q01:R41-END
}

[[maybe_unused]] void write_tuple(
    [[maybe_unused]] const base::MutableBytes output,
    [[maybe_unused]] const TupleInput input
) noexcept {
    // LAB-S03.Q01:R42-BEGIN
    base::lab::missing_algorithm("S03.Q01:R42");
    // LAB-S03.Q01:R42-END
}

[[maybe_unused]] auto append_tuples(
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const storage::PageHeader& current_header,
    [[maybe_unused]] const std::span<const TupleInput> tuples
) -> base::Status {
    // LAB-S03.Q01:R43-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R43"});
    // LAB-S03.Q01:R43-END
}

[[maybe_unused]] auto insert_tuple(
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const storage::PageHeader& current_header,
    [[maybe_unused]] const std::size_t slot,
    [[maybe_unused]] const TupleInput tuple
) -> base::Result<BtreePageChange> {
    // LAB-S03.Q01:R44-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R44"});
    // LAB-S03.Q01:R44-END
}

[[maybe_unused]] auto insert_tuple_pair(
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const storage::PageHeader& current_header,
    [[maybe_unused]] const std::size_t slot,
    [[maybe_unused]] const TupleInput first_tuple,
    [[maybe_unused]] const TupleInput second_tuple
) -> base::Result<BtreePageChange> {
    // LAB-S03.Q01:R45-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R45"});
    // LAB-S03.Q01:R45-END
}

[[maybe_unused]] auto require_mutable_page(
    [[maybe_unused]] const storage::Page* const page,
    [[maybe_unused]] const std::string_view operation
) -> base::Status {
    // LAB-S03.Q01:R46-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R46"});
    // LAB-S03.Q01:R46-END
}

}  // namespace

BtreeMetaPage::BtreeMetaPage(
    storage::Page& page,
    const storage::PageAddress /*address*/,
    const BtreeKeyLayout& /*key_layout*/
) noexcept
    : page_{&page},
      mutable_page_{&page} {}

BtreeMetaPage::BtreeMetaPage(
    const storage::Page& page,
    const storage::PageAddress /*address*/,
    const BtreeKeyLayout& /*key_layout*/
) noexcept
    : page_{&page},
      mutable_page_{nullptr} {}

[[maybe_unused]] auto BtreeMetaPage::initialize(
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const storage::PageAddress address,
    [[maybe_unused]] const BtreeKeyLayout& key_layout
) -> base::Result<BtreeMetaPage> {
    // LAB-S03.Q01:R47-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R47"});
    // LAB-S03.Q01:R47-END
}

auto BtreeMetaPage::open(
    storage::Page& page,
    const storage::PageAddress address,
    const BtreeKeyLayout& key_layout
) -> base::Result<BtreeMetaPage> {
    return BtreeMetaPage{page, address, key_layout};
}

auto BtreeMetaPage::open(
    const storage::Page& page,
    const storage::PageAddress address,
    const BtreeKeyLayout& key_layout
) -> base::Result<BtreeMetaPage> {
    return BtreeMetaPage{page, address, key_layout};
}

auto BtreeMetaPage::from_latched_page(
    storage::Page& page,
    const storage::PageAddress address,
    const BtreeKeyLayout& key_layout
) noexcept -> BtreeMetaPage {
    return BtreeMetaPage{page, address, key_layout};
}

auto BtreeMetaPage::from_latched_page(
    const storage::Page& page,
    const storage::PageAddress address,
    const BtreeKeyLayout& key_layout
) noexcept -> BtreeMetaPage {
    return BtreeMetaPage{page, address, key_layout};
}

auto BtreeMetaPage::root() const -> base::Result<std::optional<BtreeRoot>> {
    return root_ref();
}

auto BtreeMetaPage::root_ref() const noexcept -> std::optional<BtreeRoot> {
    const auto metadata = resident_meta(*page_);
    if (metadata.root_page_id == base::kInvalidPageId) {
        return std::optional<BtreeRoot>{};
    }
    return std::optional<BtreeRoot>{BtreeRoot{
        .page_id = metadata.root_page_id,
        .level = metadata.root_level,
        .generation = metadata.root_generation,
    }};
}

auto BtreeMetaPage::root_generation() const -> base::Result<std::uint64_t> {
    return root_generation_ref();
}

auto BtreeMetaPage::root_generation_ref() const noexcept -> std::uint64_t {
    return resident_meta(*page_).root_generation;
}

[[maybe_unused]] auto BtreeMetaPage::set_root(
    [[maybe_unused]] const base::PageId page_id,
    [[maybe_unused]] const std::uint16_t level_value
) -> base::Status {
    // LAB-S03.Q01:R48-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R48"});
    // LAB-S03.Q01:R48-END
}

[[maybe_unused]] auto BtreeMetaPage::clear_root() -> base::Status {
    // LAB-S03.Q01:R49-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R49"});
    // LAB-S03.Q01:R49-END
}

BtreePage::BtreePage(storage::Page& page) noexcept
    : page_{&page},
      mutable_page_{&page} {}

BtreePage::BtreePage(const storage::Page& page) noexcept
    : page_{&page},
      mutable_page_{nullptr} {}

[[maybe_unused]] auto BtreePage::initialize(
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] const storage::PageAddress /*address*/,
    [[maybe_unused]] const BtreeKeyLayout& /*key_layout*/,
    [[maybe_unused]] const BtreePageType type_value,
    [[maybe_unused]] const std::uint16_t level_value,
    [[maybe_unused]] const bool is_root_value
) -> base::Result<BtreePage> {
    // LAB-S03.Q01:R50-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R50"});
    // LAB-S03.Q01:R50-END
}

auto BtreePage::open(
    storage::Page& page,
    const storage::PageAddress /*address*/,
    const BtreeKeyLayout& /*key_layout*/
) -> base::Result<BtreePage> {
    return BtreePage{page};
}

auto BtreePage::open(
    const storage::Page& page,
    const storage::PageAddress address,
    const BtreeKeyLayout& key_layout
) -> base::Result<BtreePage> {
    return from_latched_page(page, address, key_layout);
}

auto BtreePage::from_latched_page(storage::Page& page) noexcept -> BtreePage {
    return BtreePage{page};
}

auto BtreePage::from_latched_page(const storage::Page& page) noexcept -> BtreePage {
    BtreePage result{page};
    result.cached_header_ = resident_storage_header(page);
    const auto opaque = resident_opaque(page);
    result.cached_type_ =
        (opaque.flags & kBtreePageLeafFlag) != 0U ? BtreePageType::kLeaf : BtreePageType::kInternal;
    result.cached_level_ = opaque.level;
    result.cached_root_ = (opaque.flags & kBtreePageRootFlag) != 0U;
    result.cached_incomplete_split_ = (opaque.flags & kBtreePageIncompleteSplitFlag) != 0U;
    result.cached_links_ = BtreePageLinks{
        .previous = opaque.previous == base::kInvalidPageId
                        ? std::nullopt
                        : std::optional<base::PageId>{opaque.previous},
        .next = opaque.next == base::kInvalidPageId ? std::nullopt
                                                    : std::optional<base::PageId>{opaque.next},
    };
    result.cached_free_space_ =
        static_cast<std::size_t>(result.cached_header_.upper - result.cached_header_.lower);
    return result;
}

auto BtreePage::from_latched_page(
    storage::Page& page,
    const storage::PageAddress /*address*/,
    const BtreeKeyLayout& /*key_layout*/
) noexcept -> BtreePage {
    return from_latched_page(page);
}

auto BtreePage::from_latched_page(
    const storage::Page& page,
    const storage::PageAddress /*address*/,
    const BtreeKeyLayout& /*key_layout*/
) noexcept -> BtreePage {
    return from_latched_page(page);
}

auto BtreePage::type() const -> BtreePageType {
    if (mutable_page_ == nullptr) {
        return cached_type_;
    }
    return (resident_opaque(*page_).flags & kBtreePageLeafFlag) != 0U ? BtreePageType::kLeaf
                                                                      : BtreePageType::kInternal;
}

auto BtreePage::level() const -> std::uint16_t {
    if (mutable_page_ == nullptr) {
        return cached_level_;
    }
    return resident_opaque(*page_).level;
}

auto BtreePage::is_root() const -> bool {
    if (mutable_page_ == nullptr) {
        return cached_root_;
    }
    return (resident_opaque(*page_).flags & kBtreePageRootFlag) != 0U;
}

auto BtreePage::incomplete_split() const -> bool {
    if (mutable_page_ == nullptr) {
        return cached_incomplete_split_;
    }
    return (resident_opaque(*page_).flags & kBtreePageIncompleteSplitFlag) != 0U;
}

auto BtreePage::links() const -> base::Result<BtreePageLinks> {
    return links_ref();
}

auto BtreePage::links_ref() const noexcept -> BtreePageLinks {
    if (mutable_page_ == nullptr) {
        return cached_links_;
    }
    const auto opaque = resident_opaque(*page_);
    return BtreePageLinks{
        .previous = opaque.previous == base::kInvalidPageId
                        ? std::nullopt
                        : std::optional<base::PageId>{opaque.previous},
        .next = opaque.next == base::kInvalidPageId ? std::nullopt
                                                    : std::optional<base::PageId>{opaque.next},
    };
}

auto BtreePage::tuple_count() const -> std::size_t {
    if (mutable_page_ == nullptr) {
        return (static_cast<std::size_t>(cached_header_.lower) - storage::kPageHeaderSize)
               / kBtreeSlotSize;
    }
    const auto lower = resident_storage_header(*page_).lower;
    return (static_cast<std::size_t>(lower) - storage::kPageHeaderSize) / kBtreeSlotSize;
}

auto BtreePage::free_space() const -> std::size_t {
    if (mutable_page_ == nullptr) {
        return cached_free_space_;
    }
    const auto header = resident_storage_header(*page_);
    return static_cast<std::size_t>(header.upper - header.lower);
}

auto BtreePage::tuple(const std::size_t slot) const -> base::Result<BtreeTupleView> {
    const auto value = tuple_ref(slot);
    auto result = BtreeTupleView{
        .kind = value.kind,
        .key = value.key,
        .heap_rid = std::nullopt,
        .child_page_id = std::nullopt,
    };
    if (value.kind == BtreeTupleKind::kLeafEntry) {
        result.heap_rid = value.heap_rid();
    } else if (value.kind == BtreeTupleKind::kChildPage) {
        result.child_page_id = value.target_page_id;
    }
    return result;
}

[[maybe_unused]] auto BtreePage::append_high_key([[maybe_unused]] const base::Bytes key)
    -> base::Status {
    // LAB-S03.Q01:R51-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R51"});
    // LAB-S03.Q01:R51-END
}

[[maybe_unused]] auto BtreePage::append_leaf_entry(
    [[maybe_unused]] const base::Bytes key,
    [[maybe_unused]] const base::Rid heap_rid
) -> base::Status {
    // LAB-S03.Q01:R52-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R52"});
    // LAB-S03.Q01:R52-END
}

[[maybe_unused]] auto BtreePage::insert_leaf_entry(
    [[maybe_unused]] const std::size_t slot,
    [[maybe_unused]] const base::Bytes key,
    [[maybe_unused]] const base::Rid heap_rid
) -> base::Result<BtreePageChange> {
    // LAB-S03.Q01:R53-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R53"});
    // LAB-S03.Q01:R53-END
}

[[maybe_unused]] auto
BtreePage::append_first_child([[maybe_unused]] const base::PageId child_page_id) -> base::Status {
    // LAB-S03.Q01:R54-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R54"});
    // LAB-S03.Q01:R54-END
}

[[maybe_unused]] auto BtreePage::append_separator_and_child(
    [[maybe_unused]] const base::Bytes separator,
    [[maybe_unused]] const base::PageId child_page_id
) -> base::Status {
    // LAB-S03.Q01:R55-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R55"});
    // LAB-S03.Q01:R55-END
}

[[maybe_unused]] auto BtreePage::insert_separator_and_child(
    [[maybe_unused]] const std::size_t separator_slot,
    [[maybe_unused]] const base::Bytes separator,
    [[maybe_unused]] const base::PageId child_page_id
) -> base::Result<BtreePageChange> {
    // LAB-S03.Q01:R56-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R56"});
    // LAB-S03.Q01:R56-END
}

[[maybe_unused]] auto BtreePage::set_links([[maybe_unused]] const BtreePageLinks links_value)
    -> base::Status {
    // LAB-S03.Q01:R57-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R57"});
    // LAB-S03.Q01:R57-END
}

[[maybe_unused]] auto BtreePage::set_root([[maybe_unused]] const bool is_root_value)
    -> base::Status {
    // LAB-S03.Q01:R58-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R58"});
    // LAB-S03.Q01:R58-END
}

[[maybe_unused]] auto BtreePage::set_incomplete_split([[maybe_unused]] const bool incomplete)
    -> base::Status {
    // LAB-S03.Q01:R59-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S03.Q01:R59"});
    // LAB-S03.Q01:R59-END
}

namespace {

[[maybe_unused]] void append_bulk_tuple(
    [[maybe_unused]] storage::Page& page,
    [[maybe_unused]] storage::PageHeader& header,
    [[maybe_unused]] const BtreeTupleKind kind,
    [[maybe_unused]] const base::Bytes key,
    [[maybe_unused]] const base::PageId target_page_id,
    [[maybe_unused]] const base::SlotId target_slot_id
) noexcept {
    // LAB-S03.Q01:R60-BEGIN
    base::lab::missing_algorithm("S03.Q01:R60");
    // LAB-S03.Q01:R60-END
}

}  // namespace

[[maybe_unused]] void BtreeBulkPageBuilder::append_leaf_entries(
    [[maybe_unused]] BtreePage& page,
    [[maybe_unused]] const std::span<const BtreeBulkLeafEntry> entries
) noexcept {
    // LAB-S03.Q01:R61-BEGIN
    base::lab::missing_algorithm("S03.Q01:R61");
    // LAB-S03.Q01:R61-END
}

[[maybe_unused]] void BtreeBulkPageBuilder::append_internal_entries(
    [[maybe_unused]] BtreePage& page,
    [[maybe_unused]] const base::PageId first_child,
    [[maybe_unused]] const std::span<const BtreeBulkInternalEntry> entries
) noexcept {
    // LAB-S03.Q01:R62-BEGIN
    base::lab::missing_algorithm("S03.Q01:R62");
    // LAB-S03.Q01:R62-END
}

}  // namespace db::access
