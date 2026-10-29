#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/catalog/snapshot.hpp"
#include "db/planner/metadata_port.hpp"
#include "db/sql/catalog_port.hpp"

namespace db::engine::detail {

// One immutable catalog view shared by binding and planning during an engine
// command. It owns the snapshot and materializes only the narrow read models
// defined by the consuming SQL and planner components.
class CatalogSnapshotAdapter final : public sql::CatalogReadPort, public planner::MetadataPort {
public:
    explicit CatalogSnapshotAdapter(catalog::CatalogSnapshot snapshot);

    auto generation() const noexcept -> std::uint64_t override;

    auto lookup_table(std::string_view name) const
        -> base::Result<std::optional<sql::CatalogTable>> override;

    auto lookup_table(base::Oid oid) const
        -> base::Result<std::optional<planner::PlanningTable>> override;

    auto indexes_for_table(base::Oid table_oid) const
        -> base::Result<std::vector<planner::PlanningIndex>> override;

private:
    catalog::CatalogSnapshot snapshot_;
};

}  // namespace db::engine::detail
