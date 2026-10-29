#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "db/base/result.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "format_internal.hpp"

namespace db::catalog {

inline constexpr std::size_t kTableRecordFixedSize = 24U;
inline constexpr std::size_t kColumnRecordFixedSize = 16U;
inline constexpr std::size_t kIndexRecordFixedSize = 36U;
inline constexpr std::size_t kStatisticsRecordFixedSize = 48U;
inline constexpr std::size_t kColumnStatisticsRecordFixedSize = 44U;
inline constexpr std::uint32_t kMaximumCatalogObjects = 1'000'000U;
inline constexpr std::uint32_t kMaximumColumnsPerTable = 65'535U;

auto encode_statistics_bound(const std::optional<types::Value>& bound, types::DataType data_type)
    -> base::Result<std::vector<std::byte>>;

// Validates the complete in-memory FCAT model before encoding and after decoding. This is the one
// owner for cross-record identities, schema relationships, frontiers, and encoded-size bounds.
auto validate_catalog_image_model(const CatalogImage& image) -> base::Status;

}  // namespace db::catalog
