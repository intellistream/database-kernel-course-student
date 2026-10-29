#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace db::tpcc {

// The caller proves index < N from an enum domain or an enclosing bounded loop.
template <typename Element, std::size_t Extent>
constexpr auto array_element(std::array<Element, Extent>& values, const std::size_t index) noexcept
    -> Element& {
    return std::span<Element, Extent>{values}[index];
}

template <typename Element, std::size_t Extent>
constexpr auto
array_element(const std::array<Element, Extent>& values, const std::size_t index) noexcept
    -> const Element& {
    return std::span<const Element, Extent>{values}[index];
}

}  // namespace db::tpcc
