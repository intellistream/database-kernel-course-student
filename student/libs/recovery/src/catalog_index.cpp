#include "db/recovery/catalog_index.hpp"

#include <vector>

#include "db/access/btree_key.hpp"
#include "db/access/btree_relation.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/base/result.hpp"
#include "db/catalog/consistency.hpp"

namespace db::recovery {

BtreeCatalogValidationAdapter::BtreeCatalogValidationAdapter(
    storage::BufferPool& pool,
    access::BtreeMutationLogPort& mutation_log
) noexcept
    : pool_{&pool},
      mutation_log_{&mutation_log} {}

[[maybe_unused]] auto BtreeCatalogValidationAdapter::validate(
    [[maybe_unused]] const catalog::IndexRelationValidationRequest request
) -> base::Status {
    // LAB-S10.Q10:R01-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q10:R01"});
    // LAB-S10.Q10:R01-END
}

}  // namespace db::recovery
