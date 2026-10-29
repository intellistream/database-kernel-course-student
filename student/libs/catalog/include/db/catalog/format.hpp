#pragma once

#include <cstddef>
#include <cstdint>

#include "db/catalog/schema.hpp"

namespace db::catalog {

// Little-endian bytes spell "FCAT". This is independent of every Rust catalog version.
inline constexpr std::uint32_t kCatalogFormatMagic = 0x5441'4346U;
inline constexpr std::uint16_t kCatalogFormatVersion = 1U;
inline constexpr std::size_t kCatalogFormatHeaderSize = 64U;
inline constexpr std::size_t kCatalogFormatChecksumOffset = 12U;
inline constexpr std::size_t kDefaultMaximumCatalogImageSize = std::size_t{64U} * 1024U * 1024U;

struct CatalogDecodeOptions final {
    RowSizeLimit row_size_limit;
    std::size_t maximum_image_size{kDefaultMaximumCatalogImageSize};
    std::uint32_t maximum_tables{1'000'000U};
    std::uint32_t maximum_indexes{1'000'000U};
    std::uint32_t maximum_statistics{1'000'000U};
    std::uint32_t maximum_columns_per_table{65'535U};
};

}  // namespace db::catalog
