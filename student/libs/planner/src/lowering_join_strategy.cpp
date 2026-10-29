#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/planner/join_strategy.hpp"
#include "db/types/type_registry.hpp"

#include "lowering_internal.hpp"

namespace db::planner::detail {
namespace {

constexpr std::size_t kMinimumStrategyRelations = 2U;
constexpr std::size_t kMaximumStrategyRelations = 6U;

struct SourceExpression final {
    JoinQualificationSource source;
    const PreprocessedExpression* expression{nullptr};
};

[[maybe_unused]] auto source_before(
    [[maybe_unused]] const JoinQualificationSource left,
    [[maybe_unused]] const JoinQualificationSource right
) noexcept -> bool {
    // LAB-S07.Q06:R93-BEGIN
    base::lab::missing_algorithm("S07.Q06:R93");
    // LAB-S07.Q06:R93-END
}

[[maybe_unused]] void collect_and_conjuncts(
    [[maybe_unused]] const PreprocessedExpression* const expression,
    [[maybe_unused]] const std::size_t source_clause_index,
    [[maybe_unused]] std::size_t& conjunct_index,
    [[maybe_unused]] std::vector<SourceExpression>& destination
) {
    // LAB-S07.Q06:R94-BEGIN
    base::lab::missing_algorithm("S07.Q06:R94");
    // LAB-S07.Q06:R94-END
}

[[maybe_unused]] auto collect_source_expressions(
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> source_clauses
) -> base::Result<std::vector<SourceExpression>> {
    // LAB-S07.Q06:R95-BEGIN
    base::lab::missing_algorithm("S07.Q06:R95");
    // LAB-S07.Q06:R95-END
}

[[maybe_unused]] auto find_source_expression(
    [[maybe_unused]] const std::span<const SourceExpression> expressions,
    [[maybe_unused]] const JoinQualificationSource source
) noexcept -> const PreprocessedExpression* {
    // LAB-S07.Q06:R96-BEGIN
    base::lab::missing_algorithm("S07.Q06:R96");
    // LAB-S07.Q06:R96-END
}

[[maybe_unused]] auto source_expression_pointers(
    [[maybe_unused]] const std::span<const SourceExpression> expressions,
    [[maybe_unused]] std::vector<JoinQualificationSource> sources,
    [[maybe_unused]] const std::string& subject
) -> base::Result<std::vector<const PreprocessedExpression*>> {
    // LAB-S07.Q06:R97-BEGIN
    base::lab::missing_algorithm("S07.Q06:R97");
    // LAB-S07.Q06:R97-END
}

[[maybe_unused]] auto lower_source_filter(
    [[maybe_unused]] const std::span<const SourceExpression> expressions,
    [[maybe_unused]] std::vector<JoinQualificationSource> sources,
    [[maybe_unused]] const InputLayout& input,
    [[maybe_unused]] ParameterNamespace& parameters,
    [[maybe_unused]] const std::string& subject
) -> base::Result<PlanExpressionPtr> {
    // LAB-S07.Q06:R98-BEGIN
    base::lab::missing_algorithm("S07.Q06:R98");
    // LAB-S07.Q06:R98-END
}

[[maybe_unused]] auto collect_strategy_shape(
    [[maybe_unused]] const PreprocessedTableReference& table,
    [[maybe_unused]] std::size_t& base_relation_count
) noexcept -> bool {
    // LAB-S07.Q06:R99-BEGIN
    base::lab::missing_algorithm("S07.Q06:R99");
    // LAB-S07.Q06:R99-END
}

[[maybe_unused]] auto
strategy_shape_is_applicable([[maybe_unused]] const PreprocessedTableReference& from) noexcept
    -> bool {
    // LAB-S07.Q06:R100-BEGIN
    base::lab::missing_algorithm("S07.Q06:R100");
    // LAB-S07.Q06:R100-END
}

// Join strategies lower qualifications directly into scan and join operators.
// Expression subqueries instead need the complete join input at their
// SubPlanEval boundary, so they deliberately use the generic join lowering.
[[maybe_unused]] auto strategy_has_subquery_qualification(
    [[maybe_unused]] const PreprocessedTableReference& table
) noexcept -> bool {
    // LAB-S07.Q06:R101-BEGIN
    base::lab::missing_algorithm("S07.Q06:R101");
    // LAB-S07.Q06:R101-END
}

[[maybe_unused]] auto find_base_table(
    [[maybe_unused]] const SmallInnerJoinInputView& input,
    [[maybe_unused]] const QueryRangeTableIndex relation
) noexcept -> const PreprocessedBaseTable* {
    // LAB-S07.Q06:R102-BEGIN
    base::lab::missing_algorithm("S07.Q06:R102");
    // LAB-S07.Q06:R102-END
}

[[maybe_unused]] auto find_base_paths(
    [[maybe_unused]] const std::span<const BaseRelationPathList> paths,
    [[maybe_unused]] const QueryRangeTableIndex relation
) noexcept -> const BaseRelationPathList* {
    // LAB-S07.Q06:R103-BEGIN
    base::lab::missing_algorithm("S07.Q06:R103");
    // LAB-S07.Q06:R103-END
}

[[maybe_unused]] auto find_base_placement(
    [[maybe_unused]] const std::span<const JoinQualificationPlacement> placements,
    [[maybe_unused]] const QueryRangeTableIndex relation
) noexcept -> const JoinQualificationPlacement* {
    // LAB-S07.Q06:R104-BEGIN
    base::lab::missing_algorithm("S07.Q06:R104");
    // LAB-S07.Q06:R104-END
}

[[maybe_unused]] auto lower_strategy_base(
    [[maybe_unused]] const SmallInnerJoinInputView& input,
    [[maybe_unused]] const SmallInnerJoinPathResult& path_result,
    [[maybe_unused]] const std::span<const SourceExpression> sources,
    [[maybe_unused]] const QueryRangeTableIndex relation,
    [[maybe_unused]] const BaseRelationPath& selected_path,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S07.Q06:R105-BEGIN
    base::lab::missing_algorithm("S07.Q06:R105");
    // LAB-S07.Q06:R105-END
}

[[maybe_unused]] auto make_column_reference([[maybe_unused]] const JoinStrategyColumn& column)
    -> QueryColumnReference {
    // LAB-S07.Q06:R106-BEGIN
    base::lab::missing_algorithm("S07.Q06:R106");
    // LAB-S07.Q06:R106-END
}

[[maybe_unused]] auto lower_join_columns(
    [[maybe_unused]] const std::span<const JoinStrategyKey> keys,
    [[maybe_unused]] const InputLayout& outer,
    [[maybe_unused]] const InputLayout& inner,
    [[maybe_unused]] const std::string& subject
) -> base::Result<std::vector<PlanJoinColumnPair>> {
    // LAB-S07.Q06:R107-BEGIN
    base::lab::missing_algorithm("S07.Q06:R107");
    // LAB-S07.Q06:R107-END
}

[[maybe_unused]] auto lower_nested_loop_step(
    [[maybe_unused]] LoweredPlanInput outer,
    [[maybe_unused]] LoweredPlanInput inner,
    [[maybe_unused]] const std::span<const SourceExpression> sources,
    [[maybe_unused]] const std::vector<JoinQualificationSource>& residual_sources,
    [[maybe_unused]] const std::vector<NestedLoopParameter>& parameters,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S07.Q06:R108-BEGIN
    base::lab::missing_algorithm("S07.Q06:R108");
    // LAB-S07.Q06:R108-END
}

[[maybe_unused]] auto lower_hash_join_step(
    [[maybe_unused]] LoweredPlanInput outer,
    [[maybe_unused]] LoweredPlanInput inner,
    [[maybe_unused]] const HashJoinStrategy& strategy,
    [[maybe_unused]] const std::span<const SourceExpression> sources,
    [[maybe_unused]] const std::vector<JoinQualificationSource>& residual_sources,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S07.Q06:R109-BEGIN
    base::lab::missing_algorithm("S07.Q06:R109");
    // LAB-S07.Q06:R109-END
}

[[maybe_unused]] auto lower_merge_join_step(
    [[maybe_unused]] LoweredPlanInput outer,
    [[maybe_unused]] LoweredPlanInput inner,
    [[maybe_unused]] const MergeJoinStrategy& strategy,
    [[maybe_unused]] const std::span<const SourceExpression> sources,
    [[maybe_unused]] const std::vector<JoinQualificationSource>& residual_sources,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S07.Q06:R110-BEGIN
    base::lab::missing_algorithm("S07.Q06:R110");
    // LAB-S07.Q06:R110-END
}

[[maybe_unused]] auto lower_parameterized_index_bindings(
    [[maybe_unused]] const LoweredPlanInput& inner,
    [[maybe_unused]] const ParameterizedIndexNestedLoopJoinStrategy& strategy,
    [[maybe_unused]] const InputLayout& outer_layout
) -> base::Result<std::vector<NestedLoopParameter>> {
    // LAB-S07.Q06:R111-BEGIN
    base::lab::missing_algorithm("S07.Q06:R111");
    // LAB-S07.Q06:R111-END
}

[[maybe_unused]] auto lower_parameterized_index_nested_loop_step(
    [[maybe_unused]] LoweredPlanInput outer,
    [[maybe_unused]] LoweredPlanInput inner,
    [[maybe_unused]] const ParameterizedIndexNestedLoopJoinStrategy& strategy,
    [[maybe_unused]] const std::span<const SourceExpression> sources,
    [[maybe_unused]] const std::vector<JoinQualificationSource>& residual_sources,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S07.Q06:R112-BEGIN
    base::lab::missing_algorithm("S07.Q06:R112");
    // LAB-S07.Q06:R112-END
}

[[maybe_unused]] auto lower_strategy_step(
    [[maybe_unused]] LoweredPlanInput outer,
    [[maybe_unused]] const JoinStrategyStep& step,
    [[maybe_unused]] const SmallInnerJoinInputView& input,
    [[maybe_unused]] const SmallInnerJoinPathResult& path_result,
    [[maybe_unused]] const std::span<const SourceExpression> sources,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S07.Q06:R113-BEGIN
    base::lab::missing_algorithm("S07.Q06:R113");
    // LAB-S07.Q06:R113-END
}

[[maybe_unused]] auto initial_strategy_path([[maybe_unused]] const JoinStrategyResult& strategies)
    -> base::Result<BaseRelationPath> {
    // LAB-S07.Q06:R114-BEGIN
    base::lab::missing_algorithm("S07.Q06:R114");
    // LAB-S07.Q06:R114-END
}

[[maybe_unused]] auto lower_strategy_tree(
    [[maybe_unused]] const SmallInnerJoinInputView& input,
    [[maybe_unused]] const SmallInnerJoinPathResult& paths,
    [[maybe_unused]] const JoinStrategyResult& strategies,
    [[maybe_unused]] const std::span<const SourceExpression> sources,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S07.Q06:R115-BEGIN
    base::lab::missing_algorithm("S07.Q06:R115");
    // LAB-S07.Q06:R115-END
}

[[maybe_unused]] auto add_deferred_strategy_filter(
    [[maybe_unused]] LoweredPlanInput current,
    [[maybe_unused]] const std::span<const SourceExpression> sources,
    [[maybe_unused]] const SmallInnerJoinPathResult& paths,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<LoweredPlanInput> {
    // LAB-S07.Q06:R116-BEGIN
    base::lab::missing_algorithm("S07.Q06:R116");
    // LAB-S07.Q06:R116-END
}
}  // namespace

[[maybe_unused]] auto lower_small_inner_join_if_applicable(
    [[maybe_unused]] const PreprocessedTableReference& from,
    [[maybe_unused]] const PreprocessedExpression* const where_filter,
    [[maybe_unused]] LoweringContext& context
) -> base::Result<std::optional<LoweredPlanInput>> {
    // LAB-S07.Q06:R117-BEGIN
    base::lab::missing_algorithm("S07.Q06:R117");
    // LAB-S07.Q06:R117-END
}
}  // namespace db::planner::detail
