#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/types/data_type.hpp"

namespace db::planner {

// This is the complete table shape needed before physical planning.  It is
// intentionally schema-only: catalog statistics remain a catalog concern and
// do not influence deterministic planner choices.
struct PlanningColumn final {
    std::string name;
    types::DataType data_type{types::kUnknownType};

    auto operator==(const PlanningColumn&) const -> bool = default;
};

struct PlanningTable final {
    base::Oid oid{base::kInvalidOid};
    std::vector<PlanningColumn> columns;

    auto operator==(const PlanningTable&) const -> bool = default;
};

struct PlanningIndex final {
    base::Oid oid{base::kInvalidOid};
    std::string name;
    std::vector<std::string> column_names;
    bool unique{false};

    auto operator==(const PlanningIndex&) const -> bool = default;
};

// Planner-owned, read-only metadata boundary. One port generation must
// describe a self-consistent table schema and its indexes, but it deliberately
// omits ANALYZE rows, distinct counts, null fractions, and histograms. Port
// implementations return structured failures and do not throw. A concrete
// catalog adapter belongs in a composition layer, never in planner.
class MetadataPort {
public:
    MetadataPort() = default;
    MetadataPort(const MetadataPort&) = delete;
    auto operator=(const MetadataPort&) -> MetadataPort& = delete;
    MetadataPort(MetadataPort&&) = delete;
    auto operator=(MetadataPort&&) -> MetadataPort& = delete;
    virtual ~MetadataPort();

    virtual auto generation() const noexcept -> std::uint64_t = 0;
    virtual auto lookup_table(base::Oid oid) const
        -> base::Result<std::optional<PlanningTable>> = 0;
    virtual auto indexes_for_table(base::Oid table_oid) const
        -> base::Result<std::vector<PlanningIndex>> = 0;
};

}  // namespace db::planner
