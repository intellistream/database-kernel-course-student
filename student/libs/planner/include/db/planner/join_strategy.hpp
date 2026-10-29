#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "db/base/result.hpp"
#include "db/planner/join_order.hpp"

namespace db::planner {

// A logical current-level column is retained by value until physical lowering assigns the
// concrete input position in a physical operator. It cannot carry a bound AST
// scope, a catalog handle, or an executor tuple slot across the planner
// boundary.
struct JoinStrategyColumn final {
    QueryRangeTableIndex range_table_index{};
    QueryColumnIndex column_index{};
    types::DataType data_type{types::kUnknownType};

    auto operator==(const JoinStrategyColumn&) const -> bool = default;
};

// One direct current-level equality used as an access, merge, or hash key.
// `source` identifies the exact flattened conjunction that supplied it, so
// physical lowering can lower every remaining qualification exactly once without retaining
// a PreprocessedExpression pointer in this result.
struct JoinStrategyKey final {
    JoinQualificationSource source;
    JoinStrategyColumn outer;
    JoinStrategyColumn inner;

    auto operator==(const JoinStrategyKey&) const -> bool = default;
};

struct NestedLoopJoinStrategy final {
    auto operator==(const NestedLoopJoinStrategy&) const -> bool = default;
};

// An Index Nested Loop inner lookup completes a possibly static leading
// equality prefix with executor parameters sourced from the outer tuple. The
// namespace slot is allocated only after a complete lookup candidate has been
// proven; repeated outer columns reuse one executor slot. Every `key.source`
// remains in the join residual as a runtime recheck: an index lookup by a
// runtime NULL cannot by itself establish SQL equality.
struct ParameterizedIndexNestedLoopKey final {
    PlanIndexKeyIndex inner_key_index{};
    JoinStrategyKey key;
    PlanParameterReference parameter;

    auto operator==(const ParameterizedIndexNestedLoopKey&) const -> bool = default;
};

struct ParameterizedIndexNestedLoopJoinStrategy final {
    // The ordinal names the unparameterized base candidate. `inner_path` is
    // the owned executable derivative whose equality prefix already contains
    // both static values and the complete executor-parameter suffix. Lowering
    // validates and converts it without mutating the physical scan afterward.
    std::size_t inner_path_index{0U};
    IndexScanPath inner_path;
    std::vector<ParameterizedIndexNestedLoopKey> keys;

    auto operator==(const ParameterizedIndexNestedLoopJoinStrategy&) const -> bool = default;
};

// Path construction proves forward ASC NULLS FIRST ordering for an IndexScanPath. The
// ordinals make candidate selection observable, while the paths themselves
// are copied values rather than pointers into a metadata port or caller-owned
// candidate list. A Merge Join is emitted only for a first binary base-
// relation step, where the outer ordering has not yet been obscured by an
// earlier physical join.
struct MergeJoinStrategy final {
    std::size_t outer_path_index{0U};
    std::size_t inner_path_index{0U};
    IndexScanPath outer_path;
    IndexScanPath inner_path;
    std::vector<JoinStrategyKey> keys;

    auto operator==(const MergeJoinStrategy&) const -> bool = default;
};

struct HashJoinStrategy final {
    std::vector<JoinStrategyKey> keys;

    auto operator==(const HashJoinStrategy&) const -> bool = default;
};

using JoinStrategy = std::variant<
    NestedLoopJoinStrategy,
    ParameterizedIndexNestedLoopJoinStrategy,
    MergeJoinStrategy,
    HashJoinStrategy>;

enum class JoinStrategyKind : std::uint8_t {
    kNestedLoop,
    kParameterizedIndexNestedLoop,
    kMergeJoin,
    kHashJoin,
};

auto join_strategy_kind(const JoinStrategy& strategy) noexcept -> JoinStrategyKind;

// A strategy step owns structural order data plus a physical method
// choice. `residual_qualifications` contains every applied source not executed
// as a Merge/Hash key. Parameterized Index Nested Loop deliberately retains
// all its applied sources, including lookup keys, for its SQL runtime recheck.
struct JoinStrategyStep final {
    JoinOrderStep order;
    // The first left-deep step is one base scan and therefore has no join
    // method. Every later step has exactly one strategy.
    std::optional<JoinStrategy> strategy{};
    std::vector<JoinQualificationSource> residual_qualifications;
};

struct JoinStrategyResult final {
    std::uint64_t catalog_generation{0U};
    std::vector<JoinStrategyStep> steps;
};

// Rebuilds the deterministic left-deep order from validated owned base
// paths, then chooses exactly one no-cost strategy for each join step. It
// accepts only the 2--6 relation INNER/CROSS domain and borrows source
// expressions solely to decode the source identities already recorded in
// `qualifications`; no pointer escapes the call.
//
// Priority is deliberately structural and reproducible:
// 1. a bounded single-base outer plus an inner index with a complete safe
//    equality key uses parameterized Index Nested Loop;
// 2. a first binary base-relation step with two proven ordered indexes
//    and at least one bounded input uses Merge Join;
// 3. any safe direct equality uses Hash Join;
// 4. all remaining supported qualifications use Nested Loop.
//
// Neither catalog statistics nor row/width/page/selectivity/cost data enter
// this decision. FLOAT equality remains on the Nested Loop path until a
// future executor/access contract can prove common SQL equality and key-order
// semantics for NaN and signed zero.
auto choose_small_inner_join_strategies(
    std::span<const BaseRelationPathList> relations,
    std::span<const JoinQualification> qualifications,
    std::span<const PreprocessedExpression* const> source_clauses,
    ParameterNamespace& parameter_namespace
) -> base::Result<JoinStrategyResult>;

}  // namespace db::planner
