#include "db/catalog/index_schema.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/codec.hpp"

namespace db::catalog {
namespace {

constexpr std::uint64_t kFnvOffsetBasis = 0xcbf2'9ce4'8422'2325ULL;
constexpr std::uint64_t kFnvPrime = 0x0000'0100'0000'01b3ULL;

[[maybe_unused]] void mix_signature_byte(
    [[maybe_unused]] std::uint64_t& hash,
    [[maybe_unused]] const std::uint8_t byte
) noexcept {
    // LAB-S02.Q10:R08-BEGIN
    base::lab::missing_algorithm("S02.Q10:R08");
    // LAB-S02.Q10:R08-END
}

[[maybe_unused]] void mix_signature_u16(
    [[maybe_unused]] std::uint64_t& hash,
    [[maybe_unused]] const std::uint16_t value
) noexcept {
    // LAB-S02.Q10:R09-BEGIN
    base::lab::missing_algorithm("S02.Q10:R09");
    // LAB-S02.Q10:R09-END
}

[[maybe_unused]] void mix_signature_u32(
    [[maybe_unused]] std::uint64_t& hash,
    [[maybe_unused]] const std::uint32_t value
) noexcept {
    // LAB-S02.Q10:R10-BEGIN
    base::lab::missing_algorithm("S02.Q10:R10");
    // LAB-S02.Q10:R10-END
}
}  // namespace

[[maybe_unused]] auto
index_schema_signature([[maybe_unused]] const std::span<const types::DataType> column_types)
    -> base::Result<IndexSchemaSignature> {
    // LAB-S02.Q10:R11-BEGIN
    base::lab::missing_algorithm("S02.Q10:R11");
    // LAB-S02.Q10:R11-END
}

[[maybe_unused]] auto resolve_index_schema(
    [[maybe_unused]] const Schema& schema,
    [[maybe_unused]] const std::span<const std::string> column_names
) -> base::Result<ResolvedIndexSchema> {
    // LAB-S02.Q10:R12-BEGIN
    base::lab::missing_algorithm("S02.Q10:R12");
    // LAB-S02.Q10:R12-END
}
}  // namespace db::catalog
