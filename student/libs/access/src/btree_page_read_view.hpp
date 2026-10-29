#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "db/access/btree_page.hpp"
#include "db/base/lab_missing.hpp"

namespace db::access {

// Read-only resident-page view used by B-tree traversal. The caller must hold
// the PageReadGuard that supplied the page for the complete lifetime of this
// object and every borrowed key returned from it.
//
// BufferPool owns page residency and the B-tree mutation paths preserve the
// on-page invariants. This view therefore decodes resident pages without
// repeating format, tuple-envelope, or tuple-kind checks during traversal.
class BtreePageReadView final {
private:
    struct HeaderState final {
        std::uint16_t lower{0U};
        std::uint16_t upper{0U};
        BtreePageType type{BtreePageType::kLeaf};
        std::uint16_t level{0U};
        bool root{false};
        bool incomplete_split{false};
        base::PageId previous{base::kInvalidPageId};
        base::PageId next{base::kInvalidPageId};
    };

public:
    static auto borrow(const storage::Page& page) noexcept -> BtreePageReadView {
        const auto bytes = std::span<const std::byte, storage::kPageSize>{page.bytes()};
        constexpr std::size_t opaque_offset = storage::kPageSize - kBtreePageOpaqueSize;
        const auto previous = base::PageId{load_u32_le(bytes, opaque_offset + 8U)};
        const auto next = base::PageId{load_u32_le(bytes, opaque_offset + 12U)};
        const auto level = load_u16_le(bytes, opaque_offset + 16U);
        const auto flags = load_u16_le(bytes, opaque_offset + 18U);
        const bool leaf = (flags & kReadLeafFlag) != 0U;

        return BtreePageReadView{
            bytes,
            HeaderState{
                .lower = load_u16_le(bytes, 16U),
                .upper = load_u16_le(bytes, 18U),
                .type = leaf ? BtreePageType::kLeaf : BtreePageType::kInternal,
                .level = level,
                .root = (flags & kReadRootFlag) != 0U,
                .incomplete_split = (flags & kReadIncompleteSplitFlag) != 0U,
                .previous = previous,
                .next = next,
            },
        };
    }

    auto type() const noexcept -> BtreePageType {
        return type_;
    }

    auto level() const noexcept -> std::uint16_t {
        return level_;
    }

    auto is_root() const noexcept -> bool {
        return root_;
    }

    auto incomplete_split() const noexcept -> bool {
        return incomplete_split_;
    }

    auto links_ref() const noexcept -> BtreePageLinks {
        return BtreePageLinks{
            .previous = previous_ == base::kInvalidPageId ? std::nullopt
                                                          : std::optional<base::PageId>{previous_},
            .next =
                next_ == base::kInvalidPageId ? std::nullopt : std::optional<base::PageId>{next_},
        };
    }

    // Raw trusted sibling ID for algorithms whose page-kind contract already
    // proves that a high key has a right sibling.
    auto next_page_id_ref() const noexcept -> base::PageId {
        return next_;
    }

    auto tuple_count() const noexcept -> std::size_t {
        return (static_cast<std::size_t>(lower_) - storage::kPageHeaderSize) / kBtreeSlotSize;
    }

    auto free_space() const noexcept -> std::size_t {
        return static_cast<std::size_t>(upper_ - lower_);
    }

    auto key_ref(const std::size_t slot) const noexcept -> BtreeKeyRef {
        const auto bounds = tuple_bounds(slot);
        const auto kind = tuple_kind(bounds.offset);
        return BtreeKeyRef{
            .key = bytes_.subspan(
                static_cast<std::size_t>(bounds.offset) + kBtreeTupleHeaderSize,
                static_cast<std::size_t>(bounds.length) - kBtreeTupleHeaderSize
            ),
            .kind = kind,
        };
    }

    auto tuple_ref(const std::size_t slot) const noexcept -> BtreeTupleRef {
        const auto bounds = tuple_bounds(slot);
        const auto offset = static_cast<std::size_t>(bounds.offset);
        const auto kind = tuple_kind(bounds.offset);
        const auto key = bytes_.subspan(
            offset + kBtreeTupleHeaderSize,
            static_cast<std::size_t>(bounds.length) - kBtreeTupleHeaderSize
        );
        const auto page_id = base::PageId{load_u32_le(bytes_, offset + 4U)};
        const auto slot_id = base::SlotId{load_u16_le(bytes_, offset + 8U)};
        return BtreeTupleRef{
            .key = key,
            .target_page_id = page_id,
            .target_slot_id = slot_id,
            .kind = kind,
        };
    }

private:
    static constexpr std::uint16_t kReadLeafFlag = 1U << 0U;
    static constexpr std::uint16_t kReadRootFlag = 1U << 1U;
    static constexpr std::uint16_t kReadIncompleteSplitFlag = 1U << 2U;

    struct TupleBounds final {
        std::uint16_t offset;
        std::uint16_t length;
    };

    BtreePageReadView(
        const std::span<const std::byte, storage::kPageSize> bytes,
        const HeaderState header
    ) noexcept
        : bytes_{bytes},
          lower_{header.lower},
          upper_{header.upper},
          type_{header.type},
          level_{header.level},
          root_{header.root},
          incomplete_split_{header.incomplete_split},
          previous_{header.previous},
          next_{header.next} {}

    [[maybe_unused]] auto tuple_bounds([[maybe_unused]] const std::size_t slot) const noexcept
        -> TupleBounds {
        // LAB-S03.Q01:R63-BEGIN
        base::lab::missing_algorithm("S03.Q01:R63");
        // LAB-S03.Q01:R63-END
    }

    [[maybe_unused]] auto tuple_kind([[maybe_unused]] const std::uint16_t offset) const noexcept
        -> BtreeTupleKind {
        // LAB-S03.Q01:R64-BEGIN
        base::lab::missing_algorithm("S03.Q01:R64");
        // LAB-S03.Q01:R64-END
    }

    static auto load_u16_le(
        const std::span<const std::byte, storage::kPageSize> bytes,
        const std::size_t offset
    ) noexcept -> std::uint16_t {
        const auto low = std::to_integer<std::uint16_t>(bytes[offset]);
        const auto high = std::to_integer<std::uint16_t>(bytes[offset + 1U]);
        return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
    }

    static auto load_u32_le(
        const std::span<const std::byte, storage::kPageSize> bytes,
        const std::size_t offset
    ) noexcept -> std::uint32_t {
        return std::to_integer<std::uint32_t>(bytes[offset])
               | (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U)
               | (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U)
               | (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
    }

    std::span<const std::byte, storage::kPageSize> bytes_;
    std::uint16_t lower_{0U};
    std::uint16_t upper_{0U};
    BtreePageType type_{BtreePageType::kLeaf};
    std::uint16_t level_{0U};
    bool root_{false};
    bool incomplete_split_{false};
    base::PageId previous_{base::kInvalidPageId};
    base::PageId next_{base::kInvalidPageId};
};

}  // namespace db::access
