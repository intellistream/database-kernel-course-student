#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "db/access/btree_key.hpp"
#include "db/access/btree_page_change.hpp"
#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/page.hpp"

namespace db::access {

// Both persistent structures are independently versioned inside a generic
// storage page. Page 0 is the metapage; every other page in an index relation
// uses the B-tree page format below.
inline constexpr std::uint32_t kBtreeMetapageMagic = 0x344d'5442U;  // "BTM4" little endian.
inline constexpr std::uint16_t kBtreeMetapageVersion = 4U;
inline constexpr std::size_t kBtreeMetapageHeaderSize = 48U;

inline constexpr std::uint32_t kBtreePageMagic = 0x3550'5442U;  // "BTP5" little endian.
inline constexpr std::uint16_t kBtreePageVersion = 5U;
inline constexpr std::size_t kBtreePageOpaqueSize = 32U;
inline constexpr std::size_t kBtreeSlotSize = 4U;
inline constexpr std::size_t kBtreeTupleHeaderSize = 10U;

// Fixed-field BTP5 changes used by structural WAL records. Tuple-directory
// operations return their data-dependent ranges from BtreePage itself.
inline constexpr std::size_t kBtreeOpaqueOffset = storage::kPageSize - kBtreePageOpaqueSize;
inline constexpr std::array<BtreePageChangeRange, 2U> kBtreeSiblingLinkChangeRanges{
    BtreePageChangeRange{
        .offset = static_cast<std::uint16_t>(kBtreeOpaqueOffset + 8U),
        .length = 8U,
    },
    BtreePageChangeRange{
        .offset = static_cast<std::uint16_t>(kBtreeOpaqueOffset + 24U),
        .length = 8U,
    },
};
inline constexpr std::array<BtreePageChangeRange, 2U> kBtreeRootFlagChangeRanges{
    BtreePageChangeRange{
        .offset = static_cast<std::uint16_t>(kBtreeOpaqueOffset + 18U),
        .length = 2U,
    },
    BtreePageChangeRange{
        .offset = static_cast<std::uint16_t>(kBtreeOpaqueOffset + 24U),
        .length = 8U,
    },
};
inline constexpr std::array<BtreePageChangeRange, 2U> kBtreeIncompleteSplitChangeRanges{
    BtreePageChangeRange{
        .offset = static_cast<std::uint16_t>(kBtreeOpaqueOffset + 18U),
        .length = 2U,
    },
    BtreePageChangeRange{
        .offset = static_cast<std::uint16_t>(kBtreeOpaqueOffset + 24U),
        .length = 8U,
    },
};
inline constexpr std::array<BtreePageChangeRange, 2U> kBtreeMetaRootChangeRanges{
    BtreePageChangeRange{
        .offset = static_cast<std::uint16_t>(storage::kPageHeaderSize + 12U),
        .length = 6U,
    },
    BtreePageChangeRange{
        .offset = static_cast<std::uint16_t>(storage::kPageHeaderSize + 32U),
        .length = 8U,
    },
};

enum class BtreePageType : std::uint8_t {
    kLeaf,
    kInternal,
};

enum class BtreeTupleKind : std::uint8_t {
    kHighKey = 1U,
    kLeafEntry = 2U,
    kChildPage = 3U,
    kSeparator = 4U,
};

struct BtreePageLinks final {
    std::optional<base::PageId> previous;
    std::optional<base::PageId> next;

    auto operator==(const BtreePageLinks&) const noexcept -> bool = default;
};

struct BtreeRoot final {
    base::PageId page_id{base::kInvalidPageId};
    std::uint16_t level{0};
    std::uint64_t generation{0};

    auto operator==(const BtreeRoot&) const noexcept -> bool = default;
};

// This is a borrowed page-resident view. The BtreePage and its storage::Page
// must outlive the view and its key bytes.
struct BtreeTupleView final {
    BtreeTupleKind kind{BtreeTupleKind::kHighKey};
    base::Bytes key;
    std::optional<base::Rid> heap_rid;
    std::optional<base::PageId> child_page_id;
};

// Compact borrowed reference used while a caller retains the page latch. Slot
// indexes and tuple bytes come from the trusted resident-page contract.
struct BtreeTupleRef final {
    base::Bytes key;
    base::PageId target_page_id{base::kInvalidPageId};
    base::SlotId target_slot_id{base::kInvalidSlotId};
    BtreeTupleKind kind{BtreeTupleKind::kHighKey};

    auto heap_rid() const noexcept -> base::Rid {
        return base::Rid{target_page_id, target_slot_id};
    }
};

static_assert(sizeof(BtreeTupleRef) <= 24U);

struct BtreeKeyRef final {
    base::Bytes key;
    BtreeTupleKind kind{BtreeTupleKind::kHighKey};
};

static_assert(sizeof(BtreeKeyRef) <= 24U);

// Non-owning trusted metapage view. The caller supplies the relation's page
// zero and key layout and retains the BufferPool latch for this wrapper's
// lifetime. Mutable views may call set_root/clear_root; const views are for
// inspection only. No page-format or layout checks are repeated here.
class BtreeMetaPage final {
public:
    static auto
    initialize(storage::Page& page, storage::PageAddress address, const BtreeKeyLayout& key_layout)
        -> base::Result<BtreeMetaPage>;
    static auto
    open(storage::Page& page, storage::PageAddress address, const BtreeKeyLayout& key_layout)
        -> base::Result<BtreeMetaPage>;
    static auto
    open(const storage::Page& page, storage::PageAddress address, const BtreeKeyLayout& key_layout)
        -> base::Result<BtreeMetaPage>;
    static auto from_latched_page(
        storage::Page& page,
        storage::PageAddress address,
        const BtreeKeyLayout& key_layout
    ) noexcept -> BtreeMetaPage;
    static auto from_latched_page(
        const storage::Page& page,
        storage::PageAddress address,
        const BtreeKeyLayout& key_layout
    ) noexcept -> BtreeMetaPage;

    auto root() const -> base::Result<std::optional<BtreeRoot>>;
    auto root_generation() const -> base::Result<std::uint64_t>;
    auto root_ref() const noexcept -> std::optional<BtreeRoot>;
    auto root_generation_ref() const noexcept -> std::uint64_t;
    auto set_root(base::PageId page_id, std::uint16_t level) -> base::Status;
    auto clear_root() -> base::Status;

private:
    BtreeMetaPage(
        storage::Page& page,
        storage::PageAddress address,
        const BtreeKeyLayout& key_layout
    ) noexcept;
    BtreeMetaPage(
        const storage::Page& page,
        storage::PageAddress address,
        const BtreeKeyLayout& key_layout
    ) noexcept;

    const storage::Page* page_;
    storage::Page* mutable_page_;
};

// Non-owning B-tree page facade. It owns no pin or latch; callers retain those
// through storage::PageReadGuard/PageWriteGuard. Resident pages are trusted in
// the same way as BufferPool pages: the facade decodes fixed fields and reached
// slots without repeating header, footer, tuple, or sibling-link validation. A
// read-only facade caches only its fixed header/footer metadata; it never owns
// decoded tuple views.
class BtreePage final {
public:
    static auto initialize(
        storage::Page& page,
        storage::PageAddress address,
        const BtreeKeyLayout& key_layout,
        BtreePageType type,
        std::uint16_t level,
        bool is_root = false
    ) -> base::Result<BtreePage>;
    static auto
    open(storage::Page& page, storage::PageAddress address, const BtreeKeyLayout& key_layout)
        -> base::Result<BtreePage>;
    static auto
    open(const storage::Page& page, storage::PageAddress address, const BtreeKeyLayout& key_layout)
        -> base::Result<BtreePage>;
    // Borrow a page while the caller retains its BufferPool latch.
    static auto from_latched_page(storage::Page& page) noexcept -> BtreePage;
    static auto from_latched_page(const storage::Page& page) noexcept -> BtreePage;
    static auto from_latched_page(
        storage::Page& page,
        storage::PageAddress address,
        const BtreeKeyLayout& key_layout
    ) noexcept -> BtreePage;
    static auto from_latched_page(
        const storage::Page& page,
        storage::PageAddress address,
        const BtreeKeyLayout& key_layout
    ) noexcept -> BtreePage;

    auto type() const -> BtreePageType;
    auto level() const -> std::uint16_t;
    auto is_root() const -> bool;
    auto incomplete_split() const -> bool;

    auto links() const -> base::Result<BtreePageLinks>;
    auto links_ref() const noexcept -> BtreePageLinks;
    auto tuple_count() const -> std::size_t;
    auto free_space() const -> std::size_t;
    auto tuple(std::size_t slot) const -> base::Result<BtreeTupleView>;

    // Comparison-only view. Traversal does not decode RID/child fields for
    // high keys and separators that are used solely as search boundaries.
    auto key_ref(const std::size_t slot) const noexcept -> BtreeKeyRef {
        const auto bytes = base::Bytes{page_->bytes()};
        const auto load_u16 = [&bytes](const std::size_t offset) noexcept {
            const auto low = std::to_integer<std::uint16_t>(bytes[offset]);
            const auto high = std::to_integer<std::uint16_t>(bytes[offset + 1U]);
            return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
        };
        const auto directory = storage::kPageHeaderSize + slot * kBtreeSlotSize;
        const auto tuple_offset = load_u16(directory);
        const auto tuple_length = load_u16(directory + sizeof(std::uint16_t));
        const auto kind =
            static_cast<BtreeTupleKind>(std::to_integer<std::uint8_t>(bytes[tuple_offset]));
        return BtreeKeyRef{
            .key = bytes.subspan(
                tuple_offset + kBtreeTupleHeaderSize,
                tuple_length - kBtreeTupleHeaderSize
            ),
            .kind = kind,
        };
    }

    auto tuple_ref(const std::size_t slot) const noexcept -> BtreeTupleRef {
        const auto bytes = base::Bytes{page_->bytes()};
        const auto load_u16 = [&bytes](const std::size_t offset) noexcept {
            const auto low = std::to_integer<std::uint16_t>(bytes[offset]);
            const auto high = std::to_integer<std::uint16_t>(bytes[offset + 1U]);
            return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
        };
        const auto load_u32 = [&bytes](const std::size_t offset) noexcept {
            return std::to_integer<std::uint32_t>(bytes[offset])
                   | (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U)
                   | (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U)
                   | (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
        };
        const auto directory = storage::kPageHeaderSize + slot * kBtreeSlotSize;
        const auto tuple_offset = load_u16(directory);
        const auto tuple_length = load_u16(directory + sizeof(std::uint16_t));
        const auto kind =
            static_cast<BtreeTupleKind>(std::to_integer<std::uint8_t>(bytes[tuple_offset]));
        const auto key = bytes.subspan(
            tuple_offset + kBtreeTupleHeaderSize,
            tuple_length - kBtreeTupleHeaderSize
        );
        const auto page_id = base::PageId{load_u32(tuple_offset + 4U)};
        const auto slot_id = base::SlotId{load_u16(tuple_offset + 8U)};
        return BtreeTupleRef{
            .key = key,
            .target_page_id = page_id,
            .target_slot_id = slot_id,
            .kind = kind,
        };
    }

    // `initialize` constructs an empty page. Callers own tuple ordering, page
    // type, link topology, and child/RID contracts; mutations only report
    // physical capacity or storage-write failures.
    auto append_high_key(base::Bytes key) -> base::Status;
    auto append_leaf_entry(base::Bytes key, base::Rid heap_rid) -> base::Status;
    // Insert into a leaf's logical slot order without rebuilding tuple payloads.
    auto insert_leaf_entry(std::size_t slot, base::Bytes key, base::Rid heap_rid)
        -> base::Result<BtreePageChange>;
    auto append_first_child(base::PageId child_page_id) -> base::Status;
    auto append_separator_and_child(base::Bytes separator, base::PageId child_page_id)
        -> base::Status;
    // Insert one separator/downlink pair into an existing internal page
    // without rebuilding its unchanged tuple payloads.
    auto insert_separator_and_child(
        std::size_t separator_slot,
        base::Bytes separator,
        base::PageId child_page_id
    ) -> base::Result<BtreePageChange>;
    auto set_links(BtreePageLinks links) -> base::Status;
    auto set_root(bool is_root) -> base::Status;
    auto set_incomplete_split(bool incomplete) -> base::Status;

private:
    // Declares a hidden friend; its definition remains in the component-private
    // btree_page_builder.hpp and is not visible through this public header.
    friend class BtreeBulkPageBuilder;
    explicit BtreePage(storage::Page& page) noexcept;
    explicit BtreePage(const storage::Page& page) noexcept;

    const storage::Page* page_;
    storage::Page* mutable_page_;
    storage::PageHeader cached_header_;
    BtreePageType cached_type_{BtreePageType::kLeaf};
    std::uint16_t cached_level_{0U};
    bool cached_root_{false};
    bool cached_incomplete_split_{false};
    BtreePageLinks cached_links_;
    std::size_t cached_free_space_{0U};
};

}  // namespace db::access
