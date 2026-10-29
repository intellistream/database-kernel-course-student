#include "db/catalog/snapshot.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/lab_missing.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/statistics.hpp"

namespace db::catalog {
namespace {

template <typename Value, typename Predicate>
[[maybe_unused]] auto
find_owned([[maybe_unused]] const std::vector<Value>& values, [[maybe_unused]] Predicate predicate)
    -> std::optional<Value> {
    // LAB-S02.Q10:R28-BEGIN
    base::lab::missing_algorithm("S02.Q10:R28");
    // LAB-S02.Q10:R28-END
}
}  // namespace

[[maybe_unused]] CatalogSnapshot::CatalogSnapshot(
    [[maybe_unused]] const std::uint64_t generation,
    [[maybe_unused]] std::vector<TableMetadata> tables,
    [[maybe_unused]] std::vector<IndexMetadata> indexes,
    [[maybe_unused]] std::vector<TableStatistics> statistics
)
    : generation_{generation},
      tables_{std::move(tables)},
      indexes_{std::move(indexes)},
      statistics_{std::move(statistics)} {
    // LAB-S02.Q10:R29-BEGIN
    base::lab::missing_algorithm("S02.Q10:R29");
    // LAB-S02.Q10:R29-END
}

auto CatalogSnapshot::generation() const noexcept -> std::uint64_t {
    return generation_;
}

[[maybe_unused]] auto
CatalogSnapshot::lookup_table([[maybe_unused]] const std::string_view name) const
    -> std::optional<TableMetadata> {
    // LAB-S02.Q10:R30-BEGIN
    base::lab::missing_algorithm("S02.Q10:R30");
    // LAB-S02.Q10:R30-END
}

[[maybe_unused]] auto CatalogSnapshot::lookup_table([[maybe_unused]] const base::Oid oid) const
    -> std::optional<TableMetadata> {
    // LAB-S02.Q10:R31-BEGIN
    base::lab::missing_algorithm("S02.Q10:R31");
    // LAB-S02.Q10:R31-END
}

[[maybe_unused]] auto CatalogSnapshot::list_tables() const -> std::vector<TableMetadata> {
    // LAB-S02.Q10:R32-BEGIN
    base::lab::missing_algorithm("S02.Q10:R32");
    // LAB-S02.Q10:R32-END
}

[[maybe_unused]] auto
CatalogSnapshot::lookup_index([[maybe_unused]] const std::string_view name) const
    -> std::optional<IndexMetadata> {
    // LAB-S02.Q10:R33-BEGIN
    base::lab::missing_algorithm("S02.Q10:R33");
    // LAB-S02.Q10:R33-END
}

[[maybe_unused]] auto CatalogSnapshot::list_indexes() const -> std::vector<IndexMetadata> {
    // LAB-S02.Q10:R34-BEGIN
    base::lab::missing_algorithm("S02.Q10:R34");
    // LAB-S02.Q10:R34-END
}

[[maybe_unused]] auto
CatalogSnapshot::indexes_for_table([[maybe_unused]] const base::Oid table_oid) const
    -> std::vector<IndexMetadata> {
    // LAB-S02.Q10:R35-BEGIN
    base::lab::missing_algorithm("S02.Q10:R35");
    // LAB-S02.Q10:R35-END
}

[[maybe_unused]] auto
CatalogSnapshot::table_statistics([[maybe_unused]] const base::Oid table_oid) const
    -> std::optional<TableStatistics> {
    // LAB-S02.Q10:R36-BEGIN
    base::lab::missing_algorithm("S02.Q10:R36");
    // LAB-S02.Q10:R36-END
}
}  // namespace db::catalog
