#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/type_registry.hpp"

#include "lowering_internal.hpp"

namespace db::planner::detail {
namespace {

template <typename... Visitors>
struct Overloaded final : Visitors... {
    using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

[[maybe_unused]] auto lower_index_bounds(
    [[maybe_unused]] const IndexPathBounds& bounds,
    [[maybe_unused]] ParameterNamespace& parameters
) -> base::Result<std::vector<PlanIndexBound>> {
    // LAB-S02.Q05:R53-BEGIN
    base::lab::missing_algorithm("S02.Q05:R53");
    // LAB-S02.Q05:R53-END
}

[[maybe_unused]] auto validate_path_snapshot(
    [[maybe_unused]] const BaseRelationPathList& paths,
    [[maybe_unused]] const PreprocessedBaseTable& base_table,
    [[maybe_unused]] const BaseRelationPath& selected_path,
    [[maybe_unused]] const LoweringContext& context
) -> base::Status {
    // LAB-S02.Q05:R54-BEGIN
    base::lab::missing_algorithm("S02.Q05:R54");
    // LAB-S02.Q05:R54-END
}

[[maybe_unused]] auto lower_base_relation_path_impl(
    [[maybe_unused]] const PreprocessedBaseTable& base_table,
    [[maybe_unused]] const BaseRelationPathList& paths,
    [[maybe_unused]] const BaseRelationPath& selected_path,
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> filters,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S02.Q05:R55-BEGIN
    base::lab::missing_algorithm("S02.Q05:R55");
    // LAB-S02.Q05:R55-END
}

[[maybe_unused]] auto lower_base_table(
    [[maybe_unused]] const PreprocessedBaseTable& base_table,
    [[maybe_unused]] const PreprocessedExpression* const filter,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S02.Q05:R56-BEGIN
    base::lab::missing_algorithm("S02.Q05:R56");
    // LAB-S02.Q05:R56-END
}

[[maybe_unused]] auto
adapt_union_input([[maybe_unused]] Plan input, [[maybe_unused]] const PlanSchema& schema)
    -> base::Result<PlanPtr> {
    // LAB-S06.Q04:R01-BEGIN
    base::lab::missing_algorithm("S06.Q04:R01");
    // LAB-S06.Q04:R01-END
}

[[maybe_unused]] auto lower_union_table(
    [[maybe_unused]] PreprocessedUnionTable& set,
    [[maybe_unused]] const PreprocessedExpression* filter,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S06.Q04:R02-BEGIN
    base::lab::missing_algorithm("S06.Q04:R02");
    // LAB-S06.Q04:R02-END
}

auto lower_table(
    PreprocessedTableReference& table,
    const PreprocessedExpression* filter,
    LoweringContext& context
) -> base::Result<LoweredPlanInput>;

[[maybe_unused]] auto
mutable_join_condition_expression([[maybe_unused]] PreprocessedJoinTable::Condition& condition)
    -> PreprocessedExpression* {
    // LAB-S07.Q01:R09-BEGIN
    base::lab::missing_algorithm("S07.Q01:R09");
    // LAB-S07.Q01:R09-END
}

[[maybe_unused]] auto lower_join_table(
    [[maybe_unused]] PreprocessedJoinTable& join,
    [[maybe_unused]] const PreprocessedExpression* filter,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S07.Q01:R10-BEGIN
    base::lab::missing_algorithm("S07.Q01:R10");
    // LAB-S07.Q01:R10-END
}

[[maybe_unused]] auto lower_derived_table(
    [[maybe_unused]] PreprocessedDerivedTable& derived,
    [[maybe_unused]] const PreprocessedExpression* const filter,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S06.Q05:R09-BEGIN
    base::lab::missing_algorithm("S06.Q05:R09");
    // LAB-S06.Q05:R09-END
}

[[maybe_unused]] auto lower_table(
    [[maybe_unused]] PreprocessedTableReference& table,
    [[maybe_unused]] const PreprocessedExpression* const filter,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S02.Q05:R57-BEGIN
    base::lab::missing_algorithm("S02.Q05:R57");
    // LAB-S02.Q05:R57-END
}
}  // namespace

[[maybe_unused]] auto lower_base_relation_path(
    [[maybe_unused]] const PreprocessedBaseTable& base_table,
    [[maybe_unused]] const BaseRelationPathList& paths,
    [[maybe_unused]] const BaseRelationPath& selected_path,
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> filters,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S02.Q05:R58-BEGIN
    base::lab::missing_algorithm("S02.Q05:R58");
    // LAB-S02.Q05:R58-END
}

[[maybe_unused]] auto lower_query_input(
    [[maybe_unused]] PreprocessedQuery& query,
    [[maybe_unused]] const PreprocessedExpression* const filter,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S02.Q05:R59-BEGIN
    base::lab::missing_algorithm("S02.Q05:R59");
    // LAB-S02.Q05:R59-END
}
}  // namespace db::planner::detail
