#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/base/result.hpp"
#include "db/planner/path.hpp"

namespace db::planner::detail {

auto path_error(std::string message) -> base::Error;

auto index_path_bound_value_has_type(
    const IndexPathBoundValue& value,
    types::DataType expected_type,
    bool allow_null
) -> bool;

// Validates planner-owned metadata instead of depending on catalog validation
// internals. A port implementation may be remote or test supplied, so path
// construction must never assume it originated in CatalogStore.
auto validate_planning_table(const PlanningTable& table) -> base::Status;

template <typename Column>
[[maybe_unused]] auto
has_duplicate_column_names([[maybe_unused]] const std::vector<Column>& columns) -> bool {
    // LAB-S04.Q03:R44-BEGIN
    base::lab::missing_algorithm("S04.Q03:R44");
    // LAB-S04.Q03:R44-END
}

// Source clauses remain borrowed, while rewrite-derived equalities are owned
// values. The closed variant lets the matcher account for residual SQL work
// without manufacturing a logical expression for a derived fact.
struct SourceLocalRestriction final {
    const PreprocessedExpression* expression{nullptr};
};

using LocalRestriction = std::variant<SourceLocalRestriction, BaseEqualityRestriction>;

// The matcher keeps per-restriction state private to planner source.
// `bound_matched` contributes a restriction to the B+Tree key domain, while
// `fully_matched` means a source clause needs no SQL residual recheck. Source
// parameters remain rechecked for NULL/NaN safety; derived values have no
// synthetic source clause to retain.
struct IndexPathMatch final {
    IndexPathBounds bounds;
    std::vector<bool> bound_matched;
    std::vector<bool> fully_matched;
};

auto collect_base_restrictions_for_clauses(
    const PreprocessedBaseTable& base_table,
    std::span<const PreprocessedExpression* const> clauses,
    std::span<const BaseEqualityRestriction> derived_equalities
) -> base::Result<std::vector<LocalRestriction>>;

auto match_index_path_bounds(
    const PlanIndex& index,
    QueryRangeTableIndex range_table_index,
    std::span<const LocalRestriction> restrictions
) -> base::Result<IndexPathMatch>;

// Creates one independently owned, access-shaped index candidate. It receives
// only validated planner values and borrowed logical clauses for the duration
// of construction; the returned path contains no expression or port pointer.
auto build_index_scan_path(
    const PlanIndex& index,
    QueryRangeTableIndex range_table_index,
    std::span<const LocalRestriction> restrictions
) -> base::Result<IndexScanPath>;

}  // namespace db::planner::detail
