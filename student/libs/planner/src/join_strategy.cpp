#include "db/planner/join_strategy.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/codec.hpp"
#include "db/types/type_registry.hpp"

namespace db::planner {
namespace {

struct SourceExpression final {
    JoinQualificationSource source;
    const PreprocessedExpression* expression{nullptr};
};

struct ParameterizedIndexNestedLoopDraftKey final {
    PlanIndexKeyIndex inner_key_index{};
    JoinStrategyKey key;
};

struct ParameterizedIndexNestedLoopDraft final {
    std::size_t inner_path_index{0U};
    IndexScanPath inner_path;
    std::vector<ParameterizedIndexNestedLoopDraftKey> keys;
};

[[maybe_unused]] auto join_strategy_error(std::string message) -> base::Error {
    return {base::ErrorCode::kPlan, std::move(message)};
}

[[maybe_unused]] auto source_before(
    [[maybe_unused]] const JoinQualificationSource left,
    [[maybe_unused]] const JoinQualificationSource right
) noexcept -> bool {
    // LAB-S07.Q06:R57-BEGIN
    base::lab::missing_algorithm("S07.Q06:R57");
    // LAB-S07.Q06:R57-END
}

[[maybe_unused]] void sort_sources([[maybe_unused]] std::vector<JoinQualificationSource>& sources) {
    // LAB-S07.Q06:R58-BEGIN
    base::lab::missing_algorithm("S07.Q06:R58");
    // LAB-S07.Q06:R58-END
}

[[maybe_unused]] auto contains_relation(
    [[maybe_unused]] const std::span<const QueryRangeTableIndex> relations,
    [[maybe_unused]] const QueryRangeTableIndex relation
) noexcept -> bool {
    // LAB-S07.Q06:R59-BEGIN
    base::lab::missing_algorithm("S07.Q06:R59");
    // LAB-S07.Q06:R59-END
}

[[maybe_unused]] auto relation_position(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const QueryRangeTableIndex relation
) noexcept -> std::optional<std::size_t> {
    // LAB-S07.Q06:R60-BEGIN
    base::lab::missing_algorithm("S07.Q06:R60");
    // LAB-S07.Q06:R60-END
}

[[maybe_unused]] auto
validate_index_keys_against_relation([[maybe_unused]] const BaseRelationPathList& relation_paths)
    -> base::Status {
    // LAB-S07.Q06:R61-BEGIN
    base::lab::missing_algorithm("S07.Q06:R61");
    // LAB-S07.Q06:R61-END
}

[[maybe_unused]] auto validate_strategy_relation_indexes(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations
) -> base::Status {
    // LAB-S07.Q06:R62-BEGIN
    base::lab::missing_algorithm("S07.Q06:R62");
    // LAB-S07.Q06:R62-END
}

[[maybe_unused]] void collect_and_conjuncts(
    [[maybe_unused]] const PreprocessedExpression* const expression,
    [[maybe_unused]] const std::size_t source_clause_index,
    [[maybe_unused]] std::size_t& conjunct_index,
    [[maybe_unused]] std::vector<SourceExpression>& destination
) {
    // LAB-S07.Q06:R63-BEGIN
    base::lab::missing_algorithm("S07.Q06:R63");
    // LAB-S07.Q06:R63-END
}

[[maybe_unused]] auto collect_source_expressions(
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> source_clauses
) -> base::Result<std::vector<SourceExpression>> {
    // LAB-S07.Q06:R64-BEGIN
    base::lab::missing_algorithm("S07.Q06:R64");
    // LAB-S07.Q06:R64-END
}

[[maybe_unused]] auto validate_source_qualification_alignment(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const std::span<const JoinQualification> qualifications,
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> source_clauses
) -> base::Status {
    // LAB-S07.Q06:R65-BEGIN
    base::lab::missing_algorithm("S07.Q06:R65");
    // LAB-S07.Q06:R65-END
}

[[maybe_unused]] auto find_source_expression(
    [[maybe_unused]] const std::span<const SourceExpression> expressions,
    [[maybe_unused]] const JoinQualificationSource source
) noexcept -> const PreprocessedExpression* {
    // LAB-S07.Q06:R66-BEGIN
    base::lab::missing_algorithm("S07.Q06:R66");
    // LAB-S07.Q06:R66-END
}

[[maybe_unused]] auto find_qualification(
    [[maybe_unused]] const std::span<const JoinQualification> qualifications,
    [[maybe_unused]] const JoinQualificationSource source
) noexcept -> const JoinQualification* {
    // LAB-S07.Q06:R67-BEGIN
    base::lab::missing_algorithm("S07.Q06:R67");
    // LAB-S07.Q06:R67-END
}

[[maybe_unused]] auto safe_join_key_type([[maybe_unused]] const types::DataType data_type) -> bool {
    // LAB-S07.Q06:R68-BEGIN
    base::lab::missing_algorithm("S07.Q06:R68");
    // LAB-S07.Q06:R68-END
}

[[maybe_unused]] auto same_column(
    [[maybe_unused]] const JoinStrategyColumn& left,
    [[maybe_unused]] const JoinStrategyColumn& right
) noexcept -> bool {
    // LAB-S07.Q06:R69-BEGIN
    base::lab::missing_algorithm("S07.Q06:R69");
    // LAB-S07.Q06:R69-END
}

[[maybe_unused]] auto same_key_pair(
    [[maybe_unused]] const JoinStrategyKey& left,
    [[maybe_unused]] const JoinStrategyKey& right
) noexcept -> bool {
    // LAB-S07.Q06:R70-BEGIN
    base::lab::missing_algorithm("S07.Q06:R70");
    // LAB-S07.Q06:R70-END
}

[[maybe_unused]] auto has_key_pair(
    [[maybe_unused]] const std::span<const JoinStrategyKey> keys,
    [[maybe_unused]] const JoinStrategyKey& candidate
) noexcept -> bool {
    // LAB-S07.Q06:R71-BEGIN
    base::lab::missing_algorithm("S07.Q06:R71");
    // LAB-S07.Q06:R71-END
}

[[maybe_unused]] auto direct_equality_key(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const JoinQualification& qualification,
    [[maybe_unused]] const QueryRangeTableIndex inner_relation,
    [[maybe_unused]] const std::span<const QueryRangeTableIndex> outer_relations
) -> std::optional<JoinStrategyKey> {
    // LAB-S07.Q06:R72-BEGIN
    base::lab::missing_algorithm("S07.Q06:R72");
    // LAB-S07.Q06:R72-END
}

[[maybe_unused]] auto collect_direct_keys(
    [[maybe_unused]] const JoinOrderStep& step,
    [[maybe_unused]] const std::span<const JoinQualification> qualifications,
    [[maybe_unused]] const std::span<const SourceExpression> source_expressions
) -> base::Result<std::vector<JoinStrategyKey>> {
    // LAB-S07.Q06:R73-BEGIN
    base::lab::missing_algorithm("S07.Q06:R73");
    // LAB-S07.Q06:R73-END
}

[[maybe_unused]] auto has_usable_index_domain([[maybe_unused]] const IndexScanPath& path) noexcept
    -> bool {
    // LAB-S07.Q06:R74-BEGIN
    base::lab::missing_algorithm("S07.Q06:R74");
    // LAB-S07.Q06:R74-END
}

[[maybe_unused]] auto is_bounded_index_path([[maybe_unused]] const BaseRelationPath& path) noexcept
    -> bool {
    // LAB-S07.Q06:R75-BEGIN
    base::lab::missing_algorithm("S07.Q06:R75");
    // LAB-S07.Q06:R75-END
}

[[maybe_unused]] auto index_identity_before(
    [[maybe_unused]] const PlanIndex& left,
    [[maybe_unused]] const PlanIndex& right
) noexcept -> bool {
    // LAB-S07.Q06:R76-BEGIN
    base::lab::missing_algorithm("S07.Q06:R76");
    // LAB-S07.Q06:R76-END
}

[[maybe_unused]] auto parameterized_candidate_before(
    [[maybe_unused]] const ParameterizedIndexNestedLoopDraft& candidate,
    [[maybe_unused]] const ParameterizedIndexNestedLoopDraft& incumbent
) noexcept -> bool {
    // LAB-S07.Q06:R77-BEGIN
    base::lab::missing_algorithm("S07.Q06:R77");
    // LAB-S07.Q06:R77-END
}

[[maybe_unused]] auto find_key_for_inner_column(
    [[maybe_unused]] const std::span<const JoinStrategyKey> keys,
    [[maybe_unused]] const PlanIndexKey& inner_key
) noexcept -> const JoinStrategyKey* {
    // LAB-S07.Q06:R78-BEGIN
    base::lab::missing_algorithm("S07.Q06:R78");
    // LAB-S07.Q06:R78-END
}

[[maybe_unused]] auto parameterized_index_nested_loop_candidate(
    [[maybe_unused]] const BaseRelationPathList& inner_paths,
    [[maybe_unused]] const std::span<const JoinStrategyKey> keys
) -> std::optional<ParameterizedIndexNestedLoopDraft> {
    // LAB-S07.Q06:R79-BEGIN
    base::lab::missing_algorithm("S07.Q06:R79");
    // LAB-S07.Q06:R79-END
}

[[maybe_unused]] auto materialize_parameterized_index_nested_loop(
    [[maybe_unused]] ParameterizedIndexNestedLoopDraft draft,
    [[maybe_unused]] ParameterNamespace& parameter_namespace
) -> base::Result<ParameterizedIndexNestedLoopJoinStrategy> {
    // LAB-S07.Q06:R80-BEGIN
    base::lab::missing_algorithm("S07.Q06:R80");
    // LAB-S07.Q06:R80-END
}

[[maybe_unused]] auto matching_key_pair(
    [[maybe_unused]] const std::span<const JoinStrategyKey> keys,
    [[maybe_unused]] const PlanIndexKey& outer_key,
    [[maybe_unused]] const PlanIndexKey& inner_key
) noexcept -> std::optional<JoinStrategyKey> {
    // LAB-S07.Q06:R81-BEGIN
    base::lab::missing_algorithm("S07.Q06:R81");
    // LAB-S07.Q06:R81-END
}

[[maybe_unused]] auto equality_bound_at(
    [[maybe_unused]] const IndexScanPath& path,
    [[maybe_unused]] const std::size_t key_index
) noexcept -> bool {
    // LAB-S07.Q06:R82-BEGIN
    base::lab::missing_algorithm("S07.Q06:R82");
    // LAB-S07.Q06:R82-END
}

[[maybe_unused]] auto merge_keys_for_paths(
    [[maybe_unused]] const IndexScanPath& outer_path,
    [[maybe_unused]] const QueryRangeTableIndex outer_relation,
    [[maybe_unused]] const IndexScanPath& inner_path,
    [[maybe_unused]] const QueryRangeTableIndex inner_relation,
    [[maybe_unused]] const std::span<const JoinStrategyKey> direct_keys
) -> std::vector<JoinStrategyKey> {
    // LAB-S07.Q06:R83-BEGIN
    base::lab::missing_algorithm("S07.Q06:R83");
    // LAB-S07.Q06:R83-END
}

[[maybe_unused]] auto merge_candidate_before(
    [[maybe_unused]] const MergeJoinStrategy& candidate,
    [[maybe_unused]] const MergeJoinStrategy& incumbent,
    [[maybe_unused]] const BaseRelationPathList& outer_paths,
    [[maybe_unused]] const BaseRelationPathList& inner_paths
) -> bool {
    // LAB-S07.Q06:R84-BEGIN
    base::lab::missing_algorithm("S07.Q06:R84");
    // LAB-S07.Q06:R84-END
}

[[maybe_unused]] auto merge_join_candidate(
    [[maybe_unused]] const BaseRelationPathList& outer_paths,
    [[maybe_unused]] const BaseRelationPathList& inner_paths,
    [[maybe_unused]] const std::span<const JoinStrategyKey> direct_keys
) -> std::optional<MergeJoinStrategy> {
    // LAB-S07.Q06:R85-BEGIN
    base::lab::missing_algorithm("S07.Q06:R85");
    // LAB-S07.Q06:R85-END
}

[[maybe_unused]] auto residual_sources(
    [[maybe_unused]] const JoinOrderStep& order,
    [[maybe_unused]] const std::span<const JoinStrategyKey> consumed_keys
) -> std::vector<JoinQualificationSource> {
    // LAB-S07.Q06:R86-BEGIN
    base::lab::missing_algorithm("S07.Q06:R86");
    // LAB-S07.Q06:R86-END
}

[[maybe_unused]] auto validate_first_step([[maybe_unused]] const JoinOrderStep& step)
    -> base::Status {
    // LAB-S07.Q06:R87-BEGIN
    base::lab::missing_algorithm("S07.Q06:R87");
    // LAB-S07.Q06:R87-END
}

[[maybe_unused]] auto initial_strategy_step([[maybe_unused]] const JoinOrderStep& order_step)
    -> base::Result<JoinStrategyStep> {
    // LAB-S07.Q06:R88-BEGIN
    base::lab::missing_algorithm("S07.Q06:R88");
    // LAB-S07.Q06:R88-END
}

[[maybe_unused]] auto try_binary_base_join_strategy(
    [[maybe_unused]] const JoinOrderStep& order_step,
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const std::span<const JoinStrategyKey> direct_keys,
    [[maybe_unused]] ParameterNamespace& parameter_namespace
) -> base::Result<std::optional<JoinStrategyStep>> {
    // LAB-S07.Q06:R89-BEGIN
    base::lab::missing_algorithm("S07.Q06:R89");
    // LAB-S07.Q06:R89-END
}

[[maybe_unused]] auto choose_noninitial_strategy_step(
    [[maybe_unused]] const JoinOrderStep& order_step,
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const std::span<const JoinQualification> qualifications,
    [[maybe_unused]] const std::span<const SourceExpression> source_expressions,
    [[maybe_unused]] ParameterNamespace& parameter_namespace
) -> base::Result<JoinStrategyStep> {
    // LAB-S07.Q06:R90-BEGIN
    base::lab::missing_algorithm("S07.Q06:R90");
    // LAB-S07.Q06:R90-END
}
}  // namespace

[[maybe_unused]] auto join_strategy_kind([[maybe_unused]] const JoinStrategy& strategy) noexcept
    -> JoinStrategyKind {
    // LAB-S07.Q06:R91-BEGIN
    base::lab::missing_algorithm("S07.Q06:R91");
    // LAB-S07.Q06:R91-END
}

[[maybe_unused]] auto choose_small_inner_join_strategies(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const std::span<const JoinQualification> qualifications,
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> source_clauses,
    [[maybe_unused]] ParameterNamespace& parameter_namespace
) -> base::Result<JoinStrategyResult> {
    // LAB-S07.Q06:R92-BEGIN
    base::lab::missing_algorithm("S07.Q06:R92");
    // LAB-S07.Q06:R92-END
}
}  // namespace db::planner
