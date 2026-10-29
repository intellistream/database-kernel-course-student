#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace db::access {

// Exact byte ranges changed by one in-place B+Tree page operation. The ranges
// are ordered and non-overlapping; WAL borrows their contents from the
// caller's after-page.
struct BtreePageChangeRange final {
    std::uint16_t offset{0U};
    std::uint16_t length{0U};

    auto operator==(const BtreePageChangeRange&) const noexcept -> bool = default;
};

inline constexpr std::size_t kMaximumBtreePageChangeRanges = 3U;

struct BtreePageChange final {
    std::array<BtreePageChangeRange, kMaximumBtreePageChangeRanges> changed{};
    std::uint8_t count{0U};

    auto ranges() const noexcept -> std::span<const BtreePageChangeRange> {
        return std::span{changed}.first(count);
    }
};

}  // namespace db::access
