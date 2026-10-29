#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"

namespace db::wal {

inline constexpr std::size_t kWalDataPageSize = 4096;
inline constexpr std::size_t kMaximumPageDeltaPatches = kWalDataPageSize;

using WalPageImage = std::array<std::byte, kWalDataPageSize>;

struct PageDeltaPatch final {
    std::uint16_t offset{0};
    std::vector<std::byte> bytes;

    auto operator==(const PageDeltaPatch&) const -> bool = default;
};

// Canonical page change set. The value itself is the decoded WAL state: its
// factories establish ordered, non-overlapping, in-page patches once, and
// encode/apply do not repeat that scan.
class PageDelta final {
public:
    PageDelta() = default;

    static auto create(std::span<const PageDeltaPatch> patches) -> base::Result<PageDelta>;
    static auto between(base::Bytes before, base::Bytes after) -> base::Result<PageDelta>;
    static auto decode(base::Bytes bytes) -> base::Result<PageDelta>;

    auto encode() const -> base::Result<std::vector<std::byte>>;
    auto apply(base::MutableBytes page) const -> base::Status;

    auto patches() const noexcept -> std::span<const PageDeltaPatch> {
        return patches_;
    }

    auto empty() const noexcept -> bool {
        return patches_.empty();
    }

    auto size() const noexcept -> std::size_t {
        return patches_.size();
    }

    auto operator==(const PageDelta&) const -> bool = default;

private:
    explicit PageDelta(std::vector<PageDeltaPatch> patches) noexcept
        : patches_{std::move(patches)} {}

    std::vector<PageDeltaPatch> patches_;
};

auto make_page_delta(base::Bytes before, base::Bytes after) -> base::Result<PageDelta>;

auto encode_page_delta(const PageDelta& delta) -> base::Result<std::vector<std::byte>>;
auto encode_page_delta(std::span<const PageDeltaPatch> patches)
    -> base::Result<std::vector<std::byte>>;

auto decode_page_delta(base::Bytes bytes) -> base::Result<PageDelta>;

auto apply_page_delta(base::MutableBytes page, const PageDelta& delta) -> base::Status;
auto apply_page_delta(base::MutableBytes page, std::span<const PageDeltaPatch> patches)
    -> base::Status;

}  // namespace db::wal
