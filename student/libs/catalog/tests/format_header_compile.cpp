#include "db/catalog/format.hpp"

static_assert(db::catalog::kCatalogFormatVersion == 1U);
static_assert(db::catalog::kCatalogFormatHeaderSize == 64U);
static_assert(db::catalog::kCatalogFormatChecksumOffset == 12U);
