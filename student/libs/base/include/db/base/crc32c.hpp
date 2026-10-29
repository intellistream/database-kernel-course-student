#pragma once

#include <cstddef>
#include <cstdint>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"

namespace db::base {

inline constexpr std::size_t kCrc32cSize = sizeof(std::uint32_t);

auto crc32c(Bytes bytes) noexcept -> std::uint32_t;

auto crc32c_with_zeroed(Bytes bytes, std::size_t offset, std::size_t length)
    -> Result<std::uint32_t>;

// Seal and verify a little-endian CRC-32C field contained in the protected bytes.
// The field is treated as zero during computation.
auto seal_crc32c(MutableBytes bytes, std::size_t checksum_offset) -> Status;
auto verify_crc32c(Bytes bytes, std::size_t checksum_offset) -> Status;

}  // namespace db::base
