#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>

namespace db::base {

namespace detail {

template <typename Derived, std::unsigned_integral Representation, bool Ordered>
class Identifier {
public:
    using RepresentationType = Representation;

    constexpr auto value() const noexcept -> Representation {
        return value_;
    }

    constexpr auto operator==(const Identifier&) const noexcept -> bool = default;

    constexpr auto operator<=>(const Identifier& other) const noexcept
        requires Ordered
    {
        return value_ <=> other.value_;
    }

private:
    friend Derived;

    constexpr explicit Identifier(const Representation value) noexcept
        : value_{value} {}

    Representation value_;
};

template <typename IdentifierType>
auto hash_identifier(const IdentifierType identifier) noexcept -> std::size_t {
    using Representation = IdentifierType::RepresentationType;
    return std::hash<Representation>{}(identifier.value());
}

}  // namespace detail

// Identifiers never convert implicitly to or from integers. This keeps values
// from unrelated domains out of comparisons, APIs, and arithmetic expressions.
class PageId final : public detail::Identifier<PageId, std::uint32_t, true> {
public:
    constexpr explicit PageId(const std::uint32_t value) noexcept
        : Identifier{value} {}
};

class FrameId final : public detail::Identifier<FrameId, std::uint32_t, true> {
public:
    constexpr explicit FrameId(const std::uint32_t value) noexcept
        : Identifier{value} {}
};

class Oid final : public detail::Identifier<Oid, std::uint32_t, true> {
public:
    constexpr explicit Oid(const std::uint32_t value) noexcept
        : Identifier{value} {}
};

class RelationId final : public detail::Identifier<RelationId, std::uint32_t, true> {
public:
    constexpr explicit RelationId(const std::uint32_t value) noexcept
        : Identifier{value} {}
};

class IndexId final : public detail::Identifier<IndexId, std::uint32_t, true> {
public:
    constexpr explicit IndexId(const std::uint32_t value) noexcept
        : Identifier{value} {}
};

class SlotId final : public detail::Identifier<SlotId, std::uint16_t, true> {
public:
    constexpr explicit SlotId(const std::uint16_t value) noexcept
        : Identifier{value} {}
};

class Lsn final : public detail::Identifier<Lsn, std::uint64_t, true> {
public:
    constexpr explicit Lsn(const std::uint64_t value) noexcept
        : Identifier{value} {}
};

// Transaction ordering requires explicit wraparound-aware semantics. Raw
// numeric ordering is deliberately unavailable until that contract is added.
class Xid final : public detail::Identifier<Xid, std::uint32_t, false> {
public:
    constexpr explicit Xid(const std::uint32_t value) noexcept
        : Identifier{value} {}
};

class TypeId final : public detail::Identifier<TypeId, std::uint32_t, true> {
public:
    constexpr explicit TypeId(const std::uint32_t value) noexcept
        : Identifier{value} {}
};

inline constexpr PageId kInvalidPageId{std::numeric_limits<std::uint32_t>::max()};
inline constexpr FrameId kInvalidFrameId{std::numeric_limits<std::uint32_t>::max()};
inline constexpr Oid kInvalidOid{0};
inline constexpr RelationId kInvalidRelationId{0};
inline constexpr IndexId kInvalidIndexId{0};
inline constexpr SlotId kInvalidSlotId{std::numeric_limits<std::uint16_t>::max()};
inline constexpr Lsn kInvalidLsn{0};
inline constexpr Xid kInvalidXid{0};

class Rid final {
public:
    constexpr Rid(const PageId page_id, const SlotId slot_id) noexcept
        : page_id_{page_id},
          slot_id_{slot_id} {}

    constexpr auto page_id() const noexcept -> PageId {
        return page_id_;
    }

    constexpr auto slot_id() const noexcept -> SlotId {
        return slot_id_;
    }

    constexpr auto operator==(const Rid&) const noexcept -> bool = default;

    constexpr auto operator<=>(const Rid& other) const noexcept {
        if (page_id_ != other.page_id_) {
            return page_id_ <=> other.page_id_;
        }
        return slot_id_ <=> other.slot_id_;
    }

private:
    PageId page_id_;
    SlotId slot_id_;
};

inline constexpr Rid kInvalidRid{kInvalidPageId, kInvalidSlotId};

static_assert(sizeof(PageId) == sizeof(std::uint32_t));
static_assert(sizeof(FrameId) == sizeof(std::uint32_t));
static_assert(sizeof(Oid) == sizeof(std::uint32_t));
static_assert(sizeof(RelationId) == sizeof(std::uint32_t));
static_assert(sizeof(IndexId) == sizeof(std::uint32_t));
static_assert(sizeof(SlotId) == sizeof(std::uint16_t));
static_assert(sizeof(Lsn) == sizeof(std::uint64_t));
static_assert(sizeof(Xid) == sizeof(std::uint32_t));
static_assert(sizeof(TypeId) == sizeof(std::uint32_t));

static_assert(std::is_trivially_copyable_v<PageId>);
static_assert(std::is_trivially_copyable_v<FrameId>);
static_assert(std::is_trivially_copyable_v<Oid>);
static_assert(std::is_trivially_copyable_v<RelationId>);
static_assert(std::is_trivially_copyable_v<IndexId>);
static_assert(std::is_trivially_copyable_v<SlotId>);
static_assert(std::is_trivially_copyable_v<Lsn>);
static_assert(std::is_trivially_copyable_v<Xid>);
static_assert(std::is_trivially_copyable_v<TypeId>);
static_assert(std::is_trivially_copyable_v<Rid>);

}  // namespace db::base

template <>
struct std::hash<db::base::PageId> {
    auto operator()(const db::base::PageId id) const noexcept -> std::size_t {
        return db::base::detail::hash_identifier(id);
    }
};

template <>
struct std::hash<db::base::FrameId> {
    auto operator()(const db::base::FrameId id) const noexcept -> std::size_t {
        return db::base::detail::hash_identifier(id);
    }
};

template <>
struct std::hash<db::base::Oid> {
    auto operator()(const db::base::Oid id) const noexcept -> std::size_t {
        return db::base::detail::hash_identifier(id);
    }
};

template <>
struct std::hash<db::base::RelationId> {
    auto operator()(const db::base::RelationId id) const noexcept -> std::size_t {
        return db::base::detail::hash_identifier(id);
    }
};

template <>
struct std::hash<db::base::IndexId> {
    auto operator()(const db::base::IndexId id) const noexcept -> std::size_t {
        return db::base::detail::hash_identifier(id);
    }
};

template <>
struct std::hash<db::base::SlotId> {
    auto operator()(const db::base::SlotId id) const noexcept -> std::size_t {
        return db::base::detail::hash_identifier(id);
    }
};

template <>
struct std::hash<db::base::Lsn> {
    auto operator()(const db::base::Lsn id) const noexcept -> std::size_t {
        return db::base::detail::hash_identifier(id);
    }
};

template <>
struct std::hash<db::base::Xid> {
    auto operator()(const db::base::Xid id) const noexcept -> std::size_t {
        return db::base::detail::hash_identifier(id);
    }
};

template <>
struct std::hash<db::base::TypeId> {
    auto operator()(const db::base::TypeId id) const noexcept -> std::size_t {
        return db::base::detail::hash_identifier(id);
    }
};

template <>
struct std::hash<db::base::Rid> {
    auto operator()(const db::base::Rid rid) const noexcept -> std::size_t {
        const auto encoded =
            (static_cast<std::uint64_t>(rid.page_id().value()) << 16U) | rid.slot_id().value();
        return std::hash<std::uint64_t>{}(encoded);
    }
};
