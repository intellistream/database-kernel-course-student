#pragma once

#include <cstddef>

namespace db::base {

// Combines two already-computed process-local hashes. The result is suitable
// for unordered containers but is never a durable or wire-format value.
constexpr auto combine_hash(const std::size_t seed, const std::size_t value_hash) noexcept
    -> std::size_t {
    constexpr std::size_t hash_combine_constant = 0x9e3779b9U;
    return seed ^ (value_hash + hash_combine_constant + (seed << 6U) + (seed >> 2U));
}

}  // namespace db::base
