#include "metadata_validation.hpp"

#include <string>
#include <string_view>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/base/text.hpp"
#include "db/catalog/schema.hpp"

namespace db::catalog {

[[maybe_unused]] auto validate_catalog_name(
    [[maybe_unused]] const std::string_view name,
    [[maybe_unused]] const std::string_view subject
) -> base::Status {
    // LAB-S02.Q10:R18-BEGIN
    base::lab::missing_algorithm("S02.Q10:R18");
    // LAB-S02.Q10:R18-END
}
}  // namespace db::catalog
