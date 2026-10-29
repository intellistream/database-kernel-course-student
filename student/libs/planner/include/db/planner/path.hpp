#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "db/base/result.hpp"
#include "db/planner/metadata_port.hpp"
#include "db/planner/plan.hpp"
#include "db/planner/preprocess.hpp"
#include "db/sql/ast_common.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::planner {

// These are intentionally values rather than logical-expression pointers. A
// path can outlive preprocessing scratch work, a metadata snapshot, and a
// caller's parameter vector. Physical lowering converts them directly to literals
// or parameter references without rebinding an AST.
struct IndexPathLiteralBoundValue final {
    types::Value value{types::Value::null(types::kUnknownType)};
};

// A parameter still owned by the preprocessed SQL tree is identified by its
// external slot. Lowering resolves it exactly once through ParameterNamespace.
struct IndexPathParameterBoundValue final {
    sql::ParameterSlot slot{sql::kInvalidParameterSlot};
    types::DataType data_type{types::kUnknownType};
};

// Rewrites and join parameterization already operate in the statement-wide
// planner namespace. Retaining that reference avoids converting an owned
// planner fact back into SQL syntax merely so lowering can resolve it again.
struct IndexPathPlanParameterBoundValue final {
    PlanParameterReference reference{};
};

using IndexPathBoundValue = std::variant<
    IndexPathLiteralBoundValue,
    IndexPathParameterBoundValue,
    IndexPathPlanParameterBoundValue>;

// A rewrite-proven equality is a base-path input in its own right. The
// original source qualifications remain authoritative residuals; this value
// supplies only an additional safe access domain and owns no logical AST.
struct BaseEqualityRestriction final {
    QueryRangeTableIndex range_table_index{};
    QueryColumnIndex column_index{};
    types::DataType data_type{types::kUnknownType};
    IndexPathBoundValue value;
};

// A range endpoint applies to the key immediately after equality_prefix. The
// access component consumes exactly this shape through BtreeScan::for_range
// and BtreeScan::for_eq_range, without planner depending on access headers.
struct IndexPathRangeEndpoint final {
    IndexPathBoundValue value;
    bool exclusive{false};
};

// An empty instance is a full index scan. Otherwise equality_prefix binds a
// contiguous leading key prefix and lower/upper bind the immediately following
// key. This deliberately rules out gaps and non-leading ranges that B+Tree
// cannot turn into one exact scan interval.
struct IndexPathBounds final {
    std::vector<IndexPathBoundValue> equality_prefix;
    std::optional<IndexPathRangeEndpoint> lower{};
    std::optional<IndexPathRangeEndpoint> upper{};
};

// Physical lowering turns direct logical ORDER BY column references into this small,
// relation-qualified DTO before asking an IndexScanPath about ordering. It
// avoids exposing move-only PreprocessedSortItem ownership in a path API.
struct IndexPathOrderRequirement final {
    QueryRangeTableIndex range_table_index{};
    QueryColumnIndex column_index{};
    sql::SortDirection direction{sql::SortDirection::kDefault};
    sql::NullOrder null_order{sql::NullOrder::kDefault};
};

struct SequentialScanPath final {};

// Counts describe semantic work rather than cardinality. A selected index
// path must have at least one key-domain restriction. Derived equalities and
// executor-bound join keys contribute to `domain_restriction_count`, while
// `residual_source_clause_count` counts only original source clauses that
// still require SQL evaluation. Neither field is a cost estimate.
struct IndexScanPath final {
    PlanIndex index;
    IndexPathBounds bounds{};
    std::size_t domain_restriction_count{0U};
    std::size_t residual_source_clause_count{0U};
};

using BaseRelationPath = std::variant<SequentialScanPath, IndexScanPath>;

enum class BaseRelationPathKind : std::uint8_t {
    kSequentialScan,
    kIndexScan,
};

// Owns all stable metadata and access alternatives for one base relation. A
// selected path is an index into `paths`, avoiding a mutable duplicate that
// could diverge from the candidate list. Selection is structural: choose the
// most constrained usable index, otherwise the sequential scan.
struct BaseRelationPathList final {
    std::uint64_t catalog_generation{0U};
    QueryRangeTableIndex range_table_index{};
    PlanRelation relation;
    std::vector<BaseRelationPath> paths;
    std::size_t selected_path_index{0U};
};

// Validates the public, access-shaped representation against typed planner
// metadata. It is shared by path construction now and physical-plan lowering
// later, so neither stage needs stringly column-name matching.
auto validate_index_path_bounds(const PlanIndex& index, const IndexPathBounds& bounds)
    -> base::Status;

// Forward B+Tree key encoding is ASC NULLS FIRST. The access layer has reverse
// traversal but the physical index path has no scan-direction field, so this
// reports only ordering executable without a Sort. Equality-bound
// leading keys are constants and may be omitted from an ORDER BY prefix.
auto index_path_satisfies_order(
    const IndexScanPath& path,
    QueryRangeTableIndex range_table_index,
    std::span<const IndexPathOrderRequirement> requirements
) noexcept -> bool;

auto base_relation_path_kind(const BaseRelationPath& path) -> BaseRelationPathKind;

// Reads a self-consistent metadata generation, validates that it agrees with
// the bound base relation, then constructs deterministic SeqScan and IndexScan
// candidates. It never stores the port reference and never reads catalog
// statistics. A cross-relation filter is left for join qualification placement.
auto build_base_relation_paths(
    const MetadataPort& metadata_port,
    const PreprocessedBaseTable& base_table,
    const PreprocessedExpression* filter
) -> base::Result<BaseRelationPathList>;

// The restriction-oriented form is the reusable composition boundary for
// join planning. It accepts an already distributed borrowed local clause
// subset plus owned rewrite-proven equalities. Nested AND nodes are split once;
// no source expression or caller-owned derived value is retained.
auto build_base_relation_paths_for_clauses(
    const MetadataPort& metadata_port,
    const PreprocessedBaseTable& base_table,
    std::span<const PreprocessedExpression* const> clauses,
    std::span<const BaseEqualityRestriction> derived_equalities = {}
) -> base::Result<BaseRelationPathList>;

}  // namespace db::planner
