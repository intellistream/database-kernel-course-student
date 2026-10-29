#include "db/planner/join_order.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

namespace db::planner {
namespace {

constexpr std::size_t kMinimumReorderableRelations = 2U;
constexpr std::size_t kMaximumReorderableRelations = 6U;

template <typename... Visitors>
struct Overloaded final : Visitors... {
    using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

struct SourceQualification final {
    JoinQualificationSource source;
    const PreprocessedExpression* expression{nullptr};
};

struct RelationColumn final {
    QueryRangeTableIndex range_table_index{};
    QueryColumnIndex column_index{};
    types::DataType data_type{types::kUnknownType};
};

struct QualificationDependencies final {
    std::vector<QueryRangeTableIndex> current_relations;
    std::vector<RelationColumn> current_columns;
    bool has_outer_reference{false};
    bool has_subquery{false};
    bool has_aggregate{false};
};

[[maybe_unused]] auto join_order_error(std::string message) -> base::Error {
    return {base::ErrorCode::kPlan, std::move(message)};
}

[[maybe_unused]] auto contains_relation(
    [[maybe_unused]] const std::span<const QueryRangeTableIndex> relations,
    [[maybe_unused]] const QueryRangeTableIndex relation
) noexcept -> bool {
    // LAB-S07.Q06:R01-BEGIN
    base::lab::missing_algorithm("S07.Q06:R01");
    // LAB-S07.Q06:R01-END
}

[[maybe_unused]] auto relation_position(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const QueryRangeTableIndex relation
) noexcept -> std::optional<std::size_t> {
    // LAB-S07.Q06:R02-BEGIN
    base::lab::missing_algorithm("S07.Q06:R02");
    // LAB-S07.Q06:R02-END
}

[[maybe_unused]] auto has_duplicate_relation(
    [[maybe_unused]] const std::span<const QueryRangeTableIndex> relations
) noexcept -> bool {
    // LAB-S07.Q06:R03-BEGIN
    base::lab::missing_algorithm("S07.Q06:R03");
    // LAB-S07.Q06:R03-END
}

[[maybe_unused]] auto validate_domain([[maybe_unused]] const InnerJoinDomain& domain)
    -> base::Status {
    // LAB-S07.Q06:R04-BEGIN
    base::lab::missing_algorithm("S07.Q06:R04");
    // LAB-S07.Q06:R04-END
}

[[maybe_unused]] auto has_usable_index_domain([[maybe_unused]] const IndexScanPath& path) noexcept
    -> bool {
    // LAB-S07.Q06:R05-BEGIN
    base::lab::missing_algorithm("S07.Q06:R05");
    // LAB-S07.Q06:R05-END
}

[[maybe_unused]] auto validate_base_path_list([[maybe_unused]] const BaseRelationPathList& paths)
    -> base::Status {
    // LAB-S07.Q06:R06-BEGIN
    base::lab::missing_algorithm("S07.Q06:R06");
    // LAB-S07.Q06:R06-END
}

[[maybe_unused]] auto validate_consistent_catalog_generation(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relation_paths
) -> base::Status {
    // LAB-S07.Q06:R07-BEGIN
    base::lab::missing_algorithm("S07.Q06:R07");
    // LAB-S07.Q06:R07-END
}

[[maybe_unused]] auto validate_relation_paths(
    [[maybe_unused]] const InnerJoinDomain& domain,
    [[maybe_unused]] const std::span<const BaseRelationPathList> relation_paths
) -> base::Status {
    // LAB-S07.Q06:R08-BEGIN
    base::lab::missing_algorithm("S07.Q06:R08");
    // LAB-S07.Q06:R08-END
}

[[maybe_unused]] auto validate_qualification_expression(
    [[maybe_unused]] const PreprocessedExpression* const expression,
    [[maybe_unused]] const std::string& subject
) -> base::Status {
    // LAB-S07.Q06:R09-BEGIN
    base::lab::missing_algorithm("S07.Q06:R09");
    // LAB-S07.Q06:R09-END
}

[[maybe_unused]] auto append_join_condition(
    [[maybe_unused]] const PreprocessedJoinTable& join,
    [[maybe_unused]] SmallInnerJoinInputView& result
) -> base::Status {
    // LAB-S07.Q06:R10-BEGIN
    base::lab::missing_algorithm("S07.Q06:R10");
    // LAB-S07.Q06:R10-END
}

[[maybe_unused]] auto collect_small_inner_join_table(
    [[maybe_unused]] const PreprocessedTableReference& table,
    [[maybe_unused]] SmallInnerJoinInputView& result
) -> base::Status {
    // LAB-S07.Q06:R11-BEGIN
    base::lab::missing_algorithm("S07.Q06:R11");
    // LAB-S07.Q06:R11-END
}

[[maybe_unused]] void collect_and_conjuncts(
    [[maybe_unused]] const PreprocessedExpression* const expression,
    [[maybe_unused]] const std::size_t source_clause_index,
    [[maybe_unused]] std::size_t& conjunct_index,
    [[maybe_unused]] std::vector<SourceQualification>& destination
) {
    // LAB-S07.Q06:R12-BEGIN
    base::lab::missing_algorithm("S07.Q06:R12");
    // LAB-S07.Q06:R12-END
}

[[maybe_unused]] auto split_source_qualifications(
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> source_clauses
) -> base::Result<std::vector<SourceQualification>> {
    // LAB-S07.Q06:R13-BEGIN
    base::lab::missing_algorithm("S07.Q06:R13");
    // LAB-S07.Q06:R13-END
}

auto append_expression_relations(
    const PreprocessedExpression& expression,
    QualificationDependencies& dependencies
) -> base::Status;

auto note_embedded_query(
    const PreprocessedQueryPtr& query,
    QualificationDependencies& dependencies,
    const std::string& subject
) -> base::Status;

[[maybe_unused]] auto append_expression_pointer_relations(
    [[maybe_unused]] const PreprocessedExpressionPtr& expression,
    [[maybe_unused]] QualificationDependencies& dependencies,
    [[maybe_unused]] const std::string& subject
) -> base::Status {
    // LAB-S07.Q06:R14-BEGIN
    base::lab::missing_algorithm("S07.Q06:R14");
    // LAB-S07.Q06:R14-END
}

[[maybe_unused]] auto append_expression_list_relations(
    [[maybe_unused]] const std::vector<PreprocessedExpressionPtr>& expressions,
    [[maybe_unused]] QualificationDependencies& dependencies,
    [[maybe_unused]] const std::string& subject
) -> base::Status {
    // LAB-S07.Q06:R15-BEGIN
    base::lab::missing_algorithm("S07.Q06:R15");
    // LAB-S07.Q06:R15-END
}

[[maybe_unused]] auto append_binary_relations(
    [[maybe_unused]] const QueryBinaryExpression& binary,
    [[maybe_unused]] QualificationDependencies& dependencies
) -> base::Status {
    // LAB-S07.Q06:R16-BEGIN
    base::lab::missing_algorithm("S07.Q06:R16");
    // LAB-S07.Q06:R16-END
}

[[maybe_unused]] auto append_in_list_relations(
    [[maybe_unused]] const QueryInListExpression& in_list,
    [[maybe_unused]] QualificationDependencies& dependencies
) -> base::Status {
    // LAB-S07.Q06:R17-BEGIN
    base::lab::missing_algorithm("S07.Q06:R17");
    // LAB-S07.Q06:R17-END
}

[[maybe_unused]] auto append_between_relations(
    [[maybe_unused]] const QueryBetweenExpression& between,
    [[maybe_unused]] QualificationDependencies& dependencies
) -> base::Status {
    // LAB-S07.Q06:R18-BEGIN
    base::lab::missing_algorithm("S07.Q06:R18");
    // LAB-S07.Q06:R18-END
}

[[maybe_unused]] auto append_in_subquery_relations(
    [[maybe_unused]] const QueryInSubqueryExpression& in_subquery,
    [[maybe_unused]] QualificationDependencies& dependencies
) -> base::Status {
    // LAB-S07.Q06:R19-BEGIN
    base::lab::missing_algorithm("S07.Q06:R19");
    // LAB-S07.Q06:R19-END
}

[[maybe_unused]] auto note_embedded_query(
    [[maybe_unused]] const PreprocessedQueryPtr& query,
    [[maybe_unused]] QualificationDependencies& dependencies,
    [[maybe_unused]] const std::string& subject
) -> base::Status {
    // LAB-S07.Q06:R20-BEGIN
    base::lab::missing_algorithm("S07.Q06:R20");
    // LAB-S07.Q06:R20-END
}

[[maybe_unused]] auto append_expression_relations(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] QualificationDependencies& dependencies
) -> base::Status {
    // LAB-S07.Q06:R21-BEGIN
    base::lab::missing_algorithm("S07.Q06:R21");
    // LAB-S07.Q06:R21-END
}

[[maybe_unused]] auto qualification_dependencies(
    [[maybe_unused]] const PreprocessedExpression& expression,
    [[maybe_unused]] const InnerJoinDomain& domain
) -> base::Result<QualificationDependencies> {
    // LAB-S07.Q06:R22-BEGIN
    base::lab::missing_algorithm("S07.Q06:R22");
    // LAB-S07.Q06:R22-END
}

[[maybe_unused]] auto validate_current_level_columns_against_paths(
    [[maybe_unused]] const QualificationDependencies& dependencies,
    [[maybe_unused]] const std::span<const BaseRelationPathList> relation_paths
) -> base::Status {
    // LAB-S07.Q06:R23-BEGIN
    base::lab::missing_algorithm("S07.Q06:R23");
    // LAB-S07.Q06:R23-END
}

[[maybe_unused]] auto validate_current_level_columns_against_tables(
    [[maybe_unused]] const QualificationDependencies& dependencies,
    [[maybe_unused]] const std::span<const PreprocessedBaseTable> base_tables
) -> base::Status {
    // LAB-S07.Q06:R24-BEGIN
    base::lab::missing_algorithm("S07.Q06:R24");
    // LAB-S07.Q06:R24-END
}

[[maybe_unused]] auto validate_qualification(
    [[maybe_unused]] const JoinQualification& qualification,
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations
) -> base::Status {
    // LAB-S07.Q06:R25-BEGIN
    base::lab::missing_algorithm("S07.Q06:R25");
    // LAB-S07.Q06:R25-END
}

[[maybe_unused]] auto has_duplicate_qualification_source(
    [[maybe_unused]] const std::span<const JoinQualification> qualifications
) noexcept -> bool {
    // LAB-S07.Q06:R26-BEGIN
    base::lab::missing_algorithm("S07.Q06:R26");
    // LAB-S07.Q06:R26-END
}

using RelationMask = std::uint32_t;

[[maybe_unused]] constexpr auto relation_bit([[maybe_unused]] const std::size_t position) noexcept
    -> RelationMask {
    // LAB-S07.Q06:R27-BEGIN
    base::lab::missing_algorithm("S07.Q06:R27");
    // LAB-S07.Q06:R27-END
}

[[maybe_unused]] auto qualification_masks(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const std::span<const JoinQualification> qualifications
) -> std::vector<RelationMask> {
    // LAB-S07.Q06:R28-BEGIN
    base::lab::missing_algorithm("S07.Q06:R28");
    // LAB-S07.Q06:R28-END
}

[[maybe_unused]] constexpr auto qualification_applies_at_step(
    [[maybe_unused]] const RelationMask qualification,
    [[maybe_unused]] const RelationMask selected,
    [[maybe_unused]] const std::size_t added_position
) noexcept -> bool {
    // LAB-S07.Q06:R29-BEGIN
    base::lab::missing_algorithm("S07.Q06:R29");
    // LAB-S07.Q06:R29-END
}

[[maybe_unused]] auto applicable_qualification_count(
    [[maybe_unused]] const std::span<const RelationMask> qualifications,
    [[maybe_unused]] const RelationMask selected,
    [[maybe_unused]] const std::size_t added_position
) noexcept -> std::size_t {
    // LAB-S07.Q06:R30-BEGIN
    base::lab::missing_algorithm("S07.Q06:R30");
    // LAB-S07.Q06:R30-END
}

[[maybe_unused]] auto applied_qualification_sources(
    [[maybe_unused]] const std::span<const RelationMask> masks,
    [[maybe_unused]] const std::span<const JoinQualification> qualifications,
    [[maybe_unused]] const RelationMask selected,
    [[maybe_unused]] const std::size_t added_position
) -> std::vector<JoinQualificationSource> {
    // LAB-S07.Q06:R31-BEGIN
    base::lab::missing_algorithm("S07.Q06:R31");
    // LAB-S07.Q06:R31-END
}

[[maybe_unused]] auto lowest_range_table_position(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations
) noexcept -> std::size_t {
    // LAB-S07.Q06:R32-BEGIN
    base::lab::missing_algorithm("S07.Q06:R32");
    // LAB-S07.Q06:R32-END
}

[[maybe_unused]] auto choose_next_position(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const std::span<const RelationMask> qualifications,
    [[maybe_unused]] const RelationMask selected
) -> std::size_t {
    // LAB-S07.Q06:R33-BEGIN
    base::lab::missing_algorithm("S07.Q06:R33");
    // LAB-S07.Q06:R33-END
}

[[maybe_unused]] auto
left_relations_for_step([[maybe_unused]] const std::span<const JoinOrderStep> selected_steps)
    -> std::vector<QueryRangeTableIndex> {
    // LAB-S07.Q06:R34-BEGIN
    base::lab::missing_algorithm("S07.Q06:R34");
    // LAB-S07.Q06:R34-END
}

[[maybe_unused]] auto make_structural_order(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const std::span<const JoinQualification> qualifications
) -> base::Result<JoinOrderSearchResult> {
    // LAB-S07.Q06:R35-BEGIN
    base::lab::missing_algorithm("S07.Q06:R35");
    // LAB-S07.Q06:R35-END
}

[[maybe_unused]] auto validate_implied_equality(
    [[maybe_unused]] const ImpliedBaseEquality& equality,
    [[maybe_unused]] const InnerJoinDomain& domain
) -> base::Status {
    // LAB-S07.Q06:R36-BEGIN
    base::lab::missing_algorithm("S07.Q06:R36");
    // LAB-S07.Q06:R36-END
}

[[maybe_unused]] auto base_placement_position(
    [[maybe_unused]] const std::span<const JoinQualificationPlacement> placements,
    [[maybe_unused]] const QueryRangeTableIndex relation
) noexcept -> std::optional<std::size_t> {
    // LAB-S07.Q06:R37-BEGIN
    base::lab::missing_algorithm("S07.Q06:R37");
    // LAB-S07.Q06:R37-END
}

[[maybe_unused]] auto
make_base_placements([[maybe_unused]] const std::span<const BaseRelationPathList> relation_paths)
    -> JoinQualificationPlacementResult {
    // LAB-S07.Q06:R38-BEGIN
    base::lab::missing_algorithm("S07.Q06:R38");
    // LAB-S07.Q06:R38-END
}

[[maybe_unused]] auto
must_defer([[maybe_unused]] const QualificationDependencies& dependencies) noexcept -> bool {
    // LAB-S07.Q06:R39-BEGIN
    base::lab::missing_algorithm("S07.Q06:R39");
    // LAB-S07.Q06:R39-END
}

[[maybe_unused]] auto attach_local_source(
    [[maybe_unused]] JoinQualificationPlacementResult& placement_result,
    [[maybe_unused]] const QueryRangeTableIndex relation,
    [[maybe_unused]] const JoinQualificationSource source
) -> base::Status {
    // LAB-S07.Q06:R40-BEGIN
    base::lab::missing_algorithm("S07.Q06:R40");
    // LAB-S07.Q06:R40-END
}

[[maybe_unused]] auto place_source_qualification(
    [[maybe_unused]] JoinQualificationPlacementResult& placement_result,
    [[maybe_unused]] const SourceQualification& source,
    [[maybe_unused]] const InnerJoinDomain& domain,
    [[maybe_unused]] const std::span<const BaseRelationPathList> relation_paths
) -> base::Status {
    // LAB-S07.Q06:R41-BEGIN
    base::lab::missing_algorithm("S07.Q06:R41");
    // LAB-S07.Q06:R41-END
}

[[maybe_unused]] auto attach_implied_equalities(
    [[maybe_unused]] JoinQualificationPlacementResult& placement_result,
    [[maybe_unused]] const std::span<const ImpliedBaseEquality> implied_equalities,
    [[maybe_unused]] const InnerJoinDomain& domain
) -> base::Status {
    // LAB-S07.Q06:R42-BEGIN
    base::lab::missing_algorithm("S07.Q06:R42");
    // LAB-S07.Q06:R42-END
}

[[maybe_unused]] auto validate_base_tables(
    [[maybe_unused]] const InnerJoinDomain& domain,
    [[maybe_unused]] const std::span<const PreprocessedBaseTable> base_tables
) -> base::Status {
    // LAB-S07.Q06:R43-BEGIN
    base::lab::missing_algorithm("S07.Q06:R43");
    // LAB-S07.Q06:R43-END
}

[[maybe_unused]] auto ensure_port_generation(
    [[maybe_unused]] const MetadataPort& metadata_port,
    [[maybe_unused]] const std::uint64_t generation
) -> base::Status {
    // LAB-S07.Q06:R44-BEGIN
    base::lab::missing_algorithm("S07.Q06:R44");
    // LAB-S07.Q06:R44-END
}

[[maybe_unused]] auto source_pointers_for_placement(
    [[maybe_unused]] const std::span<const SourceQualification> sources,
    [[maybe_unused]] const std::span<const JoinQualificationSource> selected
) -> std::vector<const PreprocessedExpression*> {
    // LAB-S07.Q06:R45-BEGIN
    base::lab::missing_algorithm("S07.Q06:R45");
    // LAB-S07.Q06:R45-END
}

[[maybe_unused]] auto local_sources_by_base_table(
    [[maybe_unused]] const std::span<const SourceQualification> source_qualifications,
    [[maybe_unused]] const InnerJoinDomain& domain,
    [[maybe_unused]] const std::span<const PreprocessedBaseTable> base_tables
) -> base::Result<std::vector<std::vector<JoinQualificationSource>>> {
    // LAB-S07.Q06:R46-BEGIN
    base::lab::missing_algorithm("S07.Q06:R46");
    // LAB-S07.Q06:R46-END
}

[[maybe_unused]] auto validate_implied_equalities(
    [[maybe_unused]] const std::span<const ImpliedBaseEquality> implied_equalities,
    [[maybe_unused]] const InnerJoinDomain& domain
) -> base::Status {
    // LAB-S07.Q06:R47-BEGIN
    base::lab::missing_algorithm("S07.Q06:R47");
    // LAB-S07.Q06:R47-END
}

[[maybe_unused]] auto
index_bound_value([[maybe_unused]] const EquivalencePseudoconstant& pseudoconstant)
    -> IndexPathBoundValue {
    // LAB-S07.Q06:R48-BEGIN
    base::lab::missing_algorithm("S07.Q06:R48");
    // LAB-S07.Q06:R48-END
}

[[maybe_unused]] auto base_equalities_for_relation(
    [[maybe_unused]] const QueryRangeTableIndex relation,
    [[maybe_unused]] const std::span<const ImpliedBaseEquality> implied_equalities
) -> std::vector<BaseEqualityRestriction> {
    // LAB-S07.Q06:R49-BEGIN
    base::lab::missing_algorithm("S07.Q06:R49");
    // LAB-S07.Q06:R49-END
}

[[maybe_unused]] auto build_local_base_paths(
    [[maybe_unused]] const MetadataPort& metadata_port,
    [[maybe_unused]] const std::uint64_t generation,
    [[maybe_unused]] const std::span<const PreprocessedBaseTable> base_tables,
    [[maybe_unused]] const std::span<const SourceQualification> source_qualifications,
    [[maybe_unused]] const std::span<const std::vector<JoinQualificationSource>> base_sources,
    [[maybe_unused]] const std::span<const ImpliedBaseEquality> implied_equalities
) -> base::Result<std::vector<BaseRelationPathList>> {
    // LAB-S07.Q06:R50-BEGIN
    base::lab::missing_algorithm("S07.Q06:R50");
    // LAB-S07.Q06:R50-END
}

[[maybe_unused]] auto validate_search_inputs(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const std::span<const JoinQualification> qualifications
) -> base::Status {
    // LAB-S07.Q06:R51-BEGIN
    base::lab::missing_algorithm("S07.Q06:R51");
    // LAB-S07.Q06:R51-END
}
}  // namespace

[[maybe_unused]] auto collect_small_inner_join_input(
    [[maybe_unused]] const PreprocessedTableReference& from,
    [[maybe_unused]] const PreprocessedExpression* const where_filter
) -> base::Result<SmallInnerJoinInputView> {
    // LAB-S07.Q06:R52-BEGIN
    base::lab::missing_algorithm("S07.Q06:R52");
    // LAB-S07.Q06:R52-END
}

[[maybe_unused]] auto derive_inner_join_qualification_placements(
    [[maybe_unused]] const InnerJoinDomain& domain,
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> source_clauses,
    [[maybe_unused]] const std::span<const BaseRelationPathList> relation_paths,
    [[maybe_unused]] const std::span<const ImpliedBaseEquality> implied_equalities
) -> base::Result<JoinQualificationPlacementResult> {
    // LAB-S07.Q06:R53-BEGIN
    base::lab::missing_algorithm("S07.Q06:R53");
    // LAB-S07.Q06:R53-END
}

[[maybe_unused]] auto choose_small_inner_join_order(
    [[maybe_unused]] const std::span<const BaseRelationPathList> relations,
    [[maybe_unused]] const std::span<const JoinQualification> qualifications
) -> base::Result<JoinOrderSearchResult> {
    // LAB-S07.Q06:R54-BEGIN
    base::lab::missing_algorithm("S07.Q06:R54");
    // LAB-S07.Q06:R54-END
}

[[maybe_unused]] auto build_small_inner_join_paths(
    [[maybe_unused]] const MetadataPort& metadata_port,
    [[maybe_unused]] const InnerJoinDomain& domain,
    [[maybe_unused]] const std::span<const PreprocessedBaseTable> base_tables,
    [[maybe_unused]] const std::span<const PreprocessedExpression* const> source_clauses,
    [[maybe_unused]] const std::span<const ImpliedBaseEquality> implied_equalities
) -> base::Result<SmallInnerJoinPathResult> {
    // LAB-S07.Q06:R55-BEGIN
    base::lab::missing_algorithm("S07.Q06:R55");
    // LAB-S07.Q06:R55-END
}

[[maybe_unused]] auto build_small_inner_join_paths(
    [[maybe_unused]] const MetadataPort& metadata_port,
    [[maybe_unused]] const SmallInnerJoinInputView& input,
    [[maybe_unused]] const ParameterNamespace& parameter_namespace
) -> base::Result<SmallInnerJoinPathResult> {
    // LAB-S07.Q06:R56-BEGIN
    base::lab::missing_algorithm("S07.Q06:R56");
    // LAB-S07.Q06:R56-END
}
}  // namespace db::planner
