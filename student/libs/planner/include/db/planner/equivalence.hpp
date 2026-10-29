#pragma once

#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "db/base/result.hpp"
#include "db/planner/plan.hpp"
#include "db/planner/preprocess.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::planner {

// The caller declares the current INNER/CROSS-join domain explicitly.  This
// prevents a reusable equivalence pass from crossing an outer-query level or
// a future outer-join nullable-side boundary.  A one-relation domain is valid
// for a consumer that wants to analyze a single local qualification set.
struct InnerJoinDomain final {
    std::vector<QueryRangeTableIndex> range_table_indexes;

    auto operator==(const InnerJoinDomain&) const -> bool = default;
};

// This value form deliberately drops QueryColumnReference::outer_query_levels:
// only current-level columns can enter an InnerJoinDomainEquivalenceResult.
// It is consequently safe to retain after the source logical tree is gone.
struct EquivalenceColumn final {
    QueryRangeTableIndex range_table_index{};
    QueryColumnIndex column_index{};
    types::DataType data_type{types::kUnknownType};

    auto operator==(const EquivalenceColumn&) const -> bool = default;
};

struct EquivalenceLiteral final {
    types::Value value{types::Value::null(types::kUnknownType)};

    auto operator==(const EquivalenceLiteral&) const -> bool = default;
};

// PlanParameterReference retains the statement-wide external/executor kind
// and typed ID allocated by the parameter namespace. The builder proves it belongs to the
// supplied ParameterNamespace but stores no reference to that namespace.
struct EquivalenceParameter final {
    PlanParameterReference reference{};

    auto operator==(const EquivalenceParameter&) const -> bool = default;
};

using EquivalencePseudoconstant = std::variant<EquivalenceLiteral, EquivalenceParameter>;
using EquivalenceMember = std::variant<EquivalenceColumn, EquivalenceLiteral, EquivalenceParameter>;

// An input equality is already typed and has no source-tree ownership. It is
// the reusable entry point for join ordering and strategy selection, including parameters that
// do not occur in PreprocessedExpression.
struct EquivalenceEquality final {
    EquivalenceMember left;
    EquivalenceMember right;

    auto operator==(const EquivalenceEquality&) const -> bool = default;
};

struct InnerDomainEquivalenceClass final {
    std::vector<EquivalenceMember> members;
    std::optional<EquivalencePseudoconstant> preferred_pseudoconstant{};

    auto operator==(const InnerDomainEquivalenceClass&) const -> bool = default;
};

// This is a derived base restriction, not a replacement for its source
// equality. Qualification placement decides where to attach it; physical lowering converts it to a
// literal or PlanParameterReference without recreating a logical expression tree.
struct ImpliedBaseEquality final {
    EquivalenceColumn column;
    EquivalencePseudoconstant pseudoconstant;

    auto operator==(const ImpliedBaseEquality&) const -> bool = default;
};

// All result members are owned values. Neither the source expressions nor the
// ParameterNamespace is retained, so this result can cross planner stages and
// be inspected by EXPLAIN/tests after preprocessing scratch memory is gone.
struct InnerDomainEquivalenceResult final {
    std::vector<InnerDomainEquivalenceClass> classes;
    std::vector<ImpliedBaseEquality> implied_base_equalities;

    auto operator==(const InnerDomainEquivalenceResult&) const -> bool = default;
};

auto equivalence_member_data_type(const EquivalenceMember& member) -> types::DataType;
auto equivalence_pseudoconstant_data_type(const EquivalencePseudoconstant& pseudoconstant)
    -> types::DataType;
auto equivalence_member_is_pseudoconstant(const EquivalenceMember& member) noexcept -> bool;

// Builds transitive equivalence classes from current-domain typed equalities.
// A non-NULL literal is preferred to either parameter kind as the stable
// anchor; ties preserve source equality/member order. UNKNOWN, NULL, NaN,
// mismatched-type, self, and out-of-domain equalities are deliberately
// ignored, matching their inability to prove a base restriction under SQL
// three-valued logic. Invalid public DTOs and namespace mismatches are kPlan
// errors instead of silently becoming optimizer facts.
auto build_inner_domain_equivalences(
    const InnerJoinDomain& domain,
    std::span<const EquivalenceEquality> equalities,
    const ParameterNamespace& parameter_namespace
) -> base::Result<InnerDomainEquivalenceResult>;

// Extracts top-level AND equality candidates from borrowed, planner-owned
// logical qualifiers, maps SQL external slots through ParameterNamespace, and
// invokes the value-only builder above. It never retains an expression pointer
// and intentionally does not infer through casts, OR, subqueries, or outer
// references.
auto derive_inner_domain_equivalences(
    const InnerJoinDomain& domain,
    std::span<const PreprocessedExpression* const> source_clauses,
    const ParameterNamespace& parameter_namespace
) -> base::Result<InnerDomainEquivalenceResult>;

}  // namespace db::planner
