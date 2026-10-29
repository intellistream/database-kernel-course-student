#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/page.hpp"

namespace db::storage {

inline constexpr std::uint32_t kFreeSpaceMapMagic = 0x314D5346U;  // "FSM1" little endian.
inline constexpr std::uint16_t kFreeSpaceMapVersion = 1;
inline constexpr std::uint16_t kMaximumPageFreeSpace = kPageSize - kPageHeaderSize;
inline constexpr std::size_t kMaximumFreeSpaceMapPages = std::size_t{16} * 1024U * 1024U;

// Compact, approximate free-space hints for one relation. Search results must
// be verified against the page under its content latch before use.
class FreeSpaceMap final {
public:
    FreeSpaceMap();
    static auto create(const std::filesystem::path& path) -> base::Result<FreeSpaceMap>;
    static auto open(const std::filesystem::path& path) -> base::Result<FreeSpaceMap>;

    FreeSpaceMap(const FreeSpaceMap&) = delete;
    auto operator=(const FreeSpaceMap&) -> FreeSpaceMap& = delete;
    FreeSpaceMap(FreeSpaceMap&& other) noexcept;
    auto operator=(FreeSpaceMap&& other) noexcept -> FreeSpaceMap&;
    ~FreeSpaceMap();

    // Moving transfers the complete map and leaves the source as an inert,
    // observable value. Work operations on that source return kUnavailable.
    auto valid() const noexcept -> bool;
    auto find_page(std::uint16_t needed_bytes) const -> base::Result<std::optional<base::PageId>>;
    auto update(base::PageId page_id, std::uint16_t free_bytes) -> base::Status;
    auto record_and_find(
        base::PageId page_id,
        std::uint16_t observed_free_bytes,
        std::uint16_t needed_bytes
    ) -> base::Result<std::optional<base::PageId>>;

    auto free_space_class(base::PageId page_id) const -> std::optional<std::uint8_t>;
    auto tracked_pages() const -> std::size_t;
    auto generation() const -> std::uint64_t;
    auto is_persistent() const noexcept -> bool;
    auto persist() -> base::Status;

private:
    struct Impl;

    explicit FreeSpaceMap(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::storage
