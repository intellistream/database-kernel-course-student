#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"

namespace db::wal {

inline constexpr std::size_t kWalCompressionMinimumSavings = 8;

enum class CompressionMethod : std::uint8_t {
    kNone = 0,
    kRle = 1,
};

struct CompressedBytes final {
    CompressionMethod method{CompressionMethod::kNone};
    std::vector<std::byte> bytes;

    auto operator==(const CompressedBytes&) const -> bool = default;
};

// Uses deterministic PackBits-style RLE only when it saves at least eight bytes.
auto compress_adaptively(base::Bytes input) -> CompressedBytes;

// Decoding is bounded both by the exact expected size and a caller-owned ceiling.
auto decompress(
    CompressionMethod method,
    base::Bytes input,
    std::size_t expected_size,
    std::size_t maximum_size
) -> base::Result<std::vector<std::byte>>;

}  // namespace db::wal
