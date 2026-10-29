#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/catalog/format.hpp"
#include "db/catalog/id_state.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/statistics.hpp"

namespace db::catalog {

// Mutable representation used only while validating, encoding, decoding, or publishing one FCAT
// generation. Product consumers access immutable CatalogSnapshot values through CatalogStore.
struct CatalogImage final {
    std::uint64_t generation{1U};
    CatalogIdState id_state;
    std::vector<TableMetadata> tables;
    std::vector<IndexMetadata> indexes;
    std::vector<TableStatistics> statistics{};

    auto operator==(const CatalogImage&) const -> bool = default;
};

// Encoding canonicalizes table, index, and statistics record order by OID without changing the
// input image.
auto encode_catalog_image(const CatalogImage& image) -> base::Result<std::vector<std::byte>>;

// Decoding accepts exactly the current C++ format. Unsupported versions are distinct from damage.
auto decode_catalog_image(base::Bytes bytes, CatalogDecodeOptions options)
    -> base::Result<CatalogImage>;

// This is the single future migration entry. With only v1 defined it validates and canonicalizes
// v1; source or target versions outside the supported migration graph are rejected explicitly.
auto upgrade_catalog_image(
    base::Bytes bytes,
    std::uint16_t target_version,
    CatalogDecodeOptions options
) -> base::Result<std::vector<std::byte>>;

}  // namespace db::catalog
