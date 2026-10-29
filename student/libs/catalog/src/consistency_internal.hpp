#pragma once

#include <vector>

#include "db/base/result.hpp"
#include "db/catalog/consistency.hpp"

#include "format_internal.hpp"

namespace db::catalog {

// Logical catalog inconsistency is catalog corruption and aborts the audit. A physical open error
// that deterministically means the relation must be rebuilt becomes an ordered diagnostic. Other
// I/O/service errors propagate because rebuilding cannot safely repair an unavailable subsystem.
auto audit_catalog_indexes(const CatalogImage& image, IndexRelationValidationPort& validation)
    -> base::Result<std::vector<IndexRebuildDiagnostic>>;

// Converts the first rebuild diagnostic to the stable kRebuildRequired API category. CatalogStore
// is the public facade; this image-level entry remains private so persistence state never enters a
// physical validation adapter's public contract.
auto require_catalog_index_consistency(
    const CatalogImage& image,
    IndexRelationValidationPort& validation
) -> base::Status;

}  // namespace db::catalog
