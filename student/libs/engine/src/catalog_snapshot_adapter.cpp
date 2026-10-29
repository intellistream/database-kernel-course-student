#include "catalog_snapshot_adapter.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "db/base/lab_missing.hpp"

namespace db::engine::detail {

[[maybe_unused]] CatalogSnapshotAdapter::CatalogSnapshotAdapter(
    [[maybe_unused]] catalog::CatalogSnapshot snapshot
)
    : snapshot_{std::move(snapshot)} {
    // LAB-S02.Q11:R01-BEGIN
    base::lab::missing_algorithm("S02.Q11:R01");
    // LAB-S02.Q11:R01-END
}

auto CatalogSnapshotAdapter::generation() const noexcept -> std::uint64_t {
    return snapshot_.generation();
}

[[maybe_unused]] auto
CatalogSnapshotAdapter::lookup_table([[maybe_unused]] const std::string_view name) const
    -> base::Result<std::optional<sql::CatalogTable>> {
    // LAB-S02.Q11:R02-BEGIN
    base::lab::missing_algorithm("S02.Q11:R02");
    // LAB-S02.Q11:R02-END
}

[[maybe_unused]] auto
CatalogSnapshotAdapter::lookup_table([[maybe_unused]] const base::Oid oid) const
    -> base::Result<std::optional<planner::PlanningTable>> {
    // LAB-S02.Q11:R03-BEGIN
    base::lab::missing_algorithm("S02.Q11:R03");
    // LAB-S02.Q11:R03-END
}

[[maybe_unused]] auto
CatalogSnapshotAdapter::indexes_for_table([[maybe_unused]] const base::Oid table_oid) const
    -> base::Result<std::vector<planner::PlanningIndex>> {
    // LAB-S02.Q11:R04-BEGIN
    base::lab::missing_algorithm("S02.Q11:R04");
    // LAB-S02.Q11:R04-END
}
}  // namespace db::engine::detail
