#include "db/wal/compression.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::wal {
namespace {

inline constexpr std::size_t kMinimumRun = 3;
inline constexpr std::size_t kMaximumRun = 130;
inline constexpr std::size_t kMaximumLiteral = 128;

[[maybe_unused]] auto run_length(
    [[maybe_unused]] const base::Bytes input,
    [[maybe_unused]] const std::size_t start
) noexcept -> std::size_t {
    // LAB-S10.Q05:R01-BEGIN
    base::lab::missing_algorithm("S10.Q05:R01");
    // LAB-S10.Q05:R01-END
}

[[maybe_unused]] auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.compression",
        "decompress"
    );
}

}  // namespace

[[maybe_unused]] auto compress_adaptively([[maybe_unused]] const base::Bytes input)
    -> CompressedBytes {
    // LAB-S10.Q05:R02-BEGIN
    base::lab::missing_algorithm("S10.Q05:R02");
    // LAB-S10.Q05:R02-END
}

[[maybe_unused]] auto decompress(
    [[maybe_unused]] const CompressionMethod method,
    [[maybe_unused]] const base::Bytes input,
    [[maybe_unused]] const std::size_t expected_size,
    [[maybe_unused]] const std::size_t maximum_size
) -> base::Result<std::vector<std::byte>> {
    // LAB-S10.Q05:R03-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R03"});
    // LAB-S10.Q05:R03-END
}

}  // namespace db::wal
