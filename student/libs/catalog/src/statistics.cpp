#include "db/catalog/statistics.hpp"

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/catalog/schema.hpp"
#include "db/types/codec.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::catalog {
namespace {

[[maybe_unused]] auto invalid_statistics(const std::string& message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, message}.with_context(
        "catalog.statistics",
        "validate"
    );
}

[[maybe_unused]] auto validate_bound(
    [[maybe_unused]] const std::optional<types::Value>& bound,
    [[maybe_unused]] const types::DataType expected,
    [[maybe_unused]] const char* const name
) -> base::Status {
    // LAB-S04.Q07:R07-BEGIN
    base::lab::missing_algorithm("S04.Q07:R07");
    // LAB-S04.Q07:R07-END
}

[[maybe_unused]] auto validate_sample_shape(
    [[maybe_unused]] const ColumnStatistics& column,
    [[maybe_unused]] const std::uint32_t sample_rows
) -> base::Result<std::uint64_t> {
    // LAB-S04.Q07:R08-BEGIN
    base::lab::missing_algorithm("S04.Q07:R08");
    // LAB-S04.Q07:R08-END
}

[[maybe_unused]] auto validate_column_statistics(
    [[maybe_unused]] const ColumnStatistics& column,
    [[maybe_unused]] const std::uint32_t sample_rows,
    [[maybe_unused]] const types::DataType expected
) -> base::Status {
    // LAB-S04.Q07:R09-BEGIN
    base::lab::missing_algorithm("S04.Q07:R09");
    // LAB-S04.Q07:R09-END
}
}  // namespace

[[maybe_unused]] auto validate_table_statistics(
    [[maybe_unused]] const TableStatistics& statistics,
    [[maybe_unused]] const Schema& schema
) -> base::Status {
    // LAB-S04.Q07:R10-BEGIN
    base::lab::missing_algorithm("S04.Q07:R10");
    // LAB-S04.Q07:R10-END
}
}  // namespace db::catalog
