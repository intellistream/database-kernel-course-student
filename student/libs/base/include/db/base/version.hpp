#pragma once

#include <compare>
#include <cstdint>
#include <string_view>

namespace db::base {

struct Version final {
    std::uint32_t major;
    std::uint32_t minor;
    std::uint32_t patch;

    auto operator<=>(const Version&) const = default;
};

inline constexpr Version kVersion{.major = 0, .minor = 1, .patch = 0};

auto version_string() noexcept -> std::string_view;

}  // namespace db::base
