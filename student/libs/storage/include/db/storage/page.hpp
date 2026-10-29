#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::storage {

inline constexpr std::size_t kPageSize = 4096;
inline constexpr std::size_t kPageHeaderSize = 32;
inline constexpr std::uint32_t kPageMagic = 0x47505246U;  // "FRPG" in little endian.
inline constexpr std::uint16_t kPageFormatVersion = 1;

enum class PageKind : std::uint16_t {
    kUninitialized = 0,
    kHeap = 1,
    kBtree = 2,
    kFreeSpaceMap = 3,
    kMetadata = 4,
};

struct PageAddress final {
    base::RelationId relation_id;
    base::PageId page_id;

    constexpr auto valid() const noexcept -> bool {
        return relation_id != base::kInvalidRelationId && page_id != base::kInvalidPageId;
    }

    constexpr auto operator==(const PageAddress&) const noexcept -> bool = default;

    constexpr auto operator<=>(const PageAddress& other) const noexcept {
        if (relation_id != other.relation_id) {
            return relation_id <=> other.relation_id;
        }
        return page_id <=> other.page_id;
    }
};

struct PageHeader final {
    std::uint32_t magic{kPageMagic};
    std::uint16_t version{kPageFormatVersion};
    std::uint16_t header_size{kPageHeaderSize};
    std::uint32_t checksum{0};
    std::uint16_t flags{0};
    PageKind kind{PageKind::kUninitialized};
    std::uint16_t lower{kPageHeaderSize};
    std::uint16_t upper{kPageSize};
    std::uint16_t special{kPageSize};
    std::uint16_t reserved{0};
    base::Lsn lsn{base::kInvalidLsn};

    auto operator==(const PageHeader&) const noexcept -> bool = default;

    auto encode(base::MutableBytes output) const -> base::Status;
    static auto decode(base::Bytes input) -> base::Result<PageHeader>;
};

class PageView;
class MutablePageView;

class Page final {
public:
    using Storage = std::array<std::byte, kPageSize>;

    static auto create(PageKind kind, PageAddress address, std::size_t special_size = 0)
        -> base::Result<Page>;
    static auto load(const Storage& bytes, PageAddress address) -> base::Result<Page>;

    auto bytes() const noexcept -> const Storage&;
    // Returns a non-owning whole-page view. The Page must outlive the view.
    auto view() const noexcept -> PageView;
    // Returns a non-owning mutable whole-page view. The Page must outlive the view.
    auto mutable_view() noexcept -> MutablePageView;

    auto header() const -> base::Result<PageHeader>;
    auto lsn() const noexcept -> base::Lsn;
    auto set_lsn(base::Lsn lsn) -> base::Status;
    auto flags() const -> base::Result<std::uint16_t>;
    auto set_flags(std::uint16_t flags) -> base::Status;

    auto view(std::size_t offset, std::size_t length) const -> base::Result<base::Bytes>;
    auto mutable_view(std::size_t offset, std::size_t length) -> base::Result<base::MutableBytes>;
    auto free_space_view() const -> base::Result<base::Bytes>;
    auto special_view() const -> base::Result<base::Bytes>;
    auto mutable_special_view() -> base::Result<base::MutableBytes>;

private:
    friend class FileDiskManager;

    explicit Page(const Storage& bytes) noexcept;

    Storage bytes_{};
};

// A read-only, non-owning view of one exact-size page image. It does not
// validate page contents; typed page facades remain the interpretation boundary.
class PageView final {
public:
    static auto from_storage(const Page::Storage& storage) noexcept -> PageView;

    auto bytes() const noexcept -> base::Bytes;
    auto header() const -> base::Result<PageHeader>;
    auto lsn() const noexcept -> base::Lsn;
    auto flags() const noexcept -> std::uint16_t;

    auto view(std::size_t offset, std::size_t length) const -> base::Result<base::Bytes>;
    auto free_space_view() const -> base::Result<base::Bytes>;
    auto special_view() const -> base::Result<base::Bytes>;

private:
    friend class MutablePageView;

    explicit PageView(base::Bytes bytes) noexcept;

    base::Bytes bytes_;
};

// A mutable, non-owning view of one exact-size page image. It is intended for
// use while the caller owns the corresponding page content latch.
class MutablePageView final {
public:
    static auto from_storage(Page::Storage& storage) noexcept -> MutablePageView;

    auto read_only() const noexcept -> PageView;
    auto bytes() const noexcept -> base::Bytes;
    auto bytes() noexcept -> base::MutableBytes;
    auto header() const -> base::Result<PageHeader>;
    auto lsn() const noexcept -> base::Lsn;
    auto flags() const noexcept -> std::uint16_t;
    void set_lsn(base::Lsn lsn) noexcept;
    void set_flags(std::uint16_t flags) noexcept;

    auto view(std::size_t offset, std::size_t length) const -> base::Result<base::Bytes>;
    auto mutable_view(std::size_t offset, std::size_t length) -> base::Result<base::MutableBytes>;
    auto free_space_view() const -> base::Result<base::Bytes>;
    auto special_view() const -> base::Result<base::Bytes>;
    auto mutable_special_view() -> base::Result<base::MutableBytes>;

private:
    explicit MutablePageView(base::MutableBytes bytes) noexcept;

    base::MutableBytes bytes_;
};

static_assert(sizeof(Page::Storage) == kPageSize);
static_assert(sizeof(Page) == kPageSize);

}  // namespace db::storage

template <>
struct std::hash<db::storage::PageAddress> {
    auto operator()(const db::storage::PageAddress address) const noexcept -> std::size_t {
        const auto relation = static_cast<std::uint64_t>(address.relation_id.value());
        const auto page = static_cast<std::uint64_t>(address.page_id.value());
        auto value = (relation << 32U) | page;
        value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
        value ^= value >> 31U;
        return value;
    }
};
