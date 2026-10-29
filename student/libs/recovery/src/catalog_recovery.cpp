#include "db/recovery/catalog_recovery.hpp"

#include "db/base/lab_missing.hpp"

namespace db::recovery {

CatalogStoreRecoveryAdapter::CatalogStoreRecoveryAdapter(catalog::CatalogStore& store) noexcept
    : store_{store} {}

[[maybe_unused]] auto CatalogStoreRecoveryAdapter::ensure_next_object_id(
    [[maybe_unused]] const std::uint32_t next_object_id
) -> base::Status {
    // LAB-S10.Q10:R02-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q10:R02"});
    // LAB-S10.Q10:R02-END
}

}  // namespace db::recovery
