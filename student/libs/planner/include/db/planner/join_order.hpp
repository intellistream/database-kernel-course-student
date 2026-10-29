#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "db/base/result.hpp"
#include "db/planner/equivalence.hpp"
#include "db/planner/path.hpp"
#include "db/planner/preprocess.hpp"

namespace db::planner {

// A source qualification is identified without retaining its AST. The
// conjunct ordinal is the depth-first, zero-based position after recursively
// splitting only AND nodes within `source_clause_index`; physical lowering can replay that
// deterministic split to lower the original expression at the chosen node.
struct JoinQualificationSource final {
    std::size_t source_clause_index{0U};
    std::size_t conjunct_index{0U};

    auto operator==(const JoinQualificationSource&) const -> bool = default;
};

// Planner ordering copies only source identity and current-level relation
// dependencies. The source expression is borrowed only while deriving this
// descriptor, so completed planner output never aliases logical AST.
struct JoinQualification final {
    JoinQualificationSource source;
    std::vector<QueryRangeTableIndex> required_relations;

    auto operator==(const JoinQualification&) const -> bool = default;
};

// A base qualification belongs to exactly one current-level relation. A join
// qualification names two or more. Relation-free, outer-reference, and
// subquery-containing clauses remain deferred: moving one to an arbitrary scan
// could change SQL error/evaluation timing before their physical lowering.
// The result is a pure routing table; no source AST is retained.
struct JoinQualificationPlacement final {
    QueryRangeTableIndex base_relation{};
    std::vector<JoinQualificationSource> source_qualifications;
    std::vector<ImpliedBaseEquality> implied_equalities;

    auto operator==(const JoinQualificationPlacement&) const -> bool = default;
};

struct JoinQualificationPlacementResult final {
    std::vector<JoinQualificationPlacement> base_placements;
    std::vector<JoinQualification> join_qualifications;
    std::vector<JoinQualificationSource> deferred_qualifications;

    auto operator==(const JoinQualificationPlacementResult&) const -> bool = default;
};

// This is the sole borrowing adapter from the preprocessed join tree. It accepts only
// a complete INNER/CROSS tree of base tables, flattens ON conditions in
// deterministic preorder (each parent before its descendants, matching
// DB), and appends the optional WHERE filter as the final source clause.
// Its pointers are valid only while the caller still owns the query; every
// completed results store source identities or values only.
struct SmallInnerJoinInputView final {
    InnerJoinDomain domain;
    std::vector<const PreprocessedBaseTable*> base_tables;
    std::vector<const PreprocessedExpression*> source_clauses;
};

auto collect_small_inner_join_input(
    const PreprocessedTableReference& from,
    const PreprocessedExpression* where_filter = nullptr
) -> base::Result<SmallInnerJoinInputView>;

struct JoinOrderStep final {
    QueryRangeTableIndex added_relation{};
    PlanRelation relation;
    BaseRelationPath selected_path;
    std::size_t source_path_index{0U};
    std::vector<QueryRangeTableIndex> left_relations;
    std::vector<JoinQualificationSource> applied_qualifications;
};

// This remains a left-deep structural ordering rather than a physical join
// tree. Strategy selection chooses a join implementation from the ordered base paths and
// per-step clauses without rerunning metadata reads. No statistic, estimated
// cardinality, or cost is part of this result.
struct JoinOrderSearchResult final {
    std::uint64_t catalog_generation{0U};
    std::vector<JoinOrderStep> steps;
};

// Extracts top-level AND source clauses and derives current-level relation
// dependencies. It never retains an expression pointer or attaches a clause.
// The supplied domain must be an INNER/CROSS-only base relation set proven by
// the caller.
auto derive_inner_join_qualification_placements(
    const InnerJoinDomain& domain,
    std::span<const PreprocessedExpression* const> source_clauses,
    std::span<const BaseRelationPathList> relation_paths,
    std::span<const ImpliedBaseEquality> implied_equalities = {}
) -> base::Result<JoinQualificationPlacementResult>;

// Chooses a bounded, deterministic left-deep order for two through six
// INNER/CROSS base relations. It starts at the lowest range-table index; each
// later step chooses the unselected relation that makes the most currently
// legal join qualifications available, breaking ties by range-table index.
// A qualification is attached exactly at the first step where all of its
// dependencies become available and it crosses the newly-added boundary.
auto choose_small_inner_join_order(
    std::span<const BaseRelationPathList> relations,
    std::span<const JoinQualification> qualifications
) -> base::Result<JoinOrderSearchResult>;

// This composition helper routes top-level WHERE/JOIN-ON conjunctions once,
// builds each base relation with only its legal local subset, derives the
// cross-relation qualifiers, then applies the structural order rule. It
// verifies that metadata does not change generation across the multi-relation
// read and returns wholly owned values. Deferred clauses are preserved for
// physical lowering rather than silently evaluated or dropped.
struct SmallInnerJoinPathResult final {
    std::vector<BaseRelationPathList> base_paths;
    JoinQualificationPlacementResult placements;
    JoinOrderSearchResult order;
};

auto build_small_inner_join_paths(
    const MetadataPort& metadata_port,
    const InnerJoinDomain& domain,
    std::span<const PreprocessedBaseTable> base_tables,
    std::span<const PreprocessedExpression* const> source_clauses,
    std::span<const ImpliedBaseEquality> implied_equalities
) -> base::Result<SmallInnerJoinPathResult>;

// The tree-adapter overload is the complete logical-to-path boundary: it
// derives current-domain equivalences through the supplied statement
// namespace before composing base paths and placements. It copies only
// base-table metadata needed by the owned result; source expressions and the
// namespace remain borrowed for the call and are never retained.
auto build_small_inner_join_paths(
    const MetadataPort& metadata_port,
    const SmallInnerJoinInputView& input,
    const ParameterNamespace& parameter_namespace
) -> base::Result<SmallInnerJoinPathResult>;

}  // namespace db::planner
