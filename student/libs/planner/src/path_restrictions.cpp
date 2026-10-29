#include <algorithm>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/lab_missing.hpp"
#include "db/planner/path.hpp"
#include "db/types/type_registry.hpp"

#include "path_internal.hpp"

namespace db::planner {
namespace {

auto expression_is_local_or_constant(
    const PreprocessedExpression& expression,
    QueryRangeTableIndex range_table_index
) -> bool;

template <typename ExpressionPointer>
[[maybe_unused]] auto expression_pointer_is_local_or_constant(
    [[maybe_unused]] const ExpressionPointer& expression,
    [[maybe_unused]] const QueryRangeTableIndex range_table_index
) -> bool {
    // LAB-S04.Q01:R01-BEGIN
    base::lab::missing_algorithm("S04.Q01:R01");
    // LAB-S04.Q01:R01-END
}

[[maybe_unused]] auto expression_list_is_local_or_constant(
    [[maybe_unused]] const std::vector<PreprocessedExpressionPtr>& expressions,
    [[maybe_unused]] const QueryRangeTableIndex range_table_index
) -> bool {
    // LAB-S04.Q01:R02-BEGIN
    base::lab::missing_algorithm("S04.Q01:R02");
    // LAB-S04.Q01:R02-END
}

[[maybe_unused]] auto expression_is_local_or_constant(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const QueryRangeTableIndex range_table_index
) -> bool {
    // LAB-S04.Q01:R03-BEGIN
    base::lab::missing_algorithm("S04.Q01:R03");
    // LAB-S04.Q01:R03-END
}

[[maybe_unused]] void collect_and_clauses(
    [[maybe_unused]] const PreprocessedExpression* expression,
    [[maybe_unused]] std::vector<const PreprocessedExpression*>& clauses
) {
    // LAB-S04.Q01:R04-BEGIN
    base::lab::missing_algorithm("S04.Q01:R04");
    // LAB-S04.Q01:R04-END
}

[[maybe_unused]] auto collect_local_clauses(
    [[maybe_unused]] const QueryRangeTableIndex range_table_index,
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> source_clauses
) -> base::Result<std::vector<detail::LocalRestriction>> {
    // LAB-S04.Q01:R05-BEGIN
    base::lab::missing_algorithm("S04.Q01:R05");
    // LAB-S04.Q01:R05-END
}

[[maybe_unused]] auto append_derived_equalities(
    [[maybe_unused]] const PreprocessedBaseTable& base_table,
    [[maybe_unused]] const std::span<const BaseEqualityRestriction> derived_equalities,
    [[maybe_unused]] std::vector<detail::LocalRestriction>& restrictions
) -> base::Status {
    // LAB-S04.Q01:R06-BEGIN
    base::lab::missing_algorithm("S04.Q01:R06");
    // LAB-S04.Q01:R06-END
}
}  // namespace

namespace detail {

auto path_error(std::string message) -> base::Error {
    return {base::ErrorCode::kPlan, std::move(message)};
}

[[maybe_unused]] auto validate_planning_table([[maybe_unused]] const PlanningTable& table)
    -> base::Status {
    // LAB-S04.Q01:R07-BEGIN
    base::lab::missing_algorithm("S04.Q01:R07");
    // LAB-S04.Q01:R07-END
}

[[maybe_unused]] auto collect_base_restrictions_for_clauses(
    [[maybe_unused]] const PreprocessedBaseTable& base_table,
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> clauses,
    [[maybe_unused]] const std::span<const BaseEqualityRestriction> derived_equalities
) -> base::Result<std::vector<LocalRestriction>> {
    // LAB-S04.Q01:R08-BEGIN
    base::lab::missing_algorithm("S04.Q01:R08");
    // LAB-S04.Q01:R08-END
}
}  // namespace detail
}  // namespace db::planner
