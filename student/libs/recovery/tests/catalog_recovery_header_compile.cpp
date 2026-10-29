#include <type_traits>

#include "db/recovery/catalog_recovery.hpp"

static_assert(
    std::is_base_of_v<db::recovery::CatalogRecoveryPort, db::recovery::CatalogStoreRecoveryAdapter>
);
