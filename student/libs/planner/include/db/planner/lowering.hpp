#pragma once

#include <cstdint>
#include <span>

#include "db/base/result.hpp"
#include "db/planner/metadata_port.hpp"
#include "db/planner/plan.hpp"
#include "db/planner/preprocess.hpp"

namespace db::sql {
struct BoundQuery;
struct BoundStatement;
struct BoundParameterSlot;
}  // namespace db::sql

namespace db::planner {

// The boundary between logical preprocessing and a completed statement is
// deliberately explicit. Lowering owns a physical SELECT tree plus the one
// metadata generation and parameter namespace used while building it; final
// assembly consumes this move-only value as a reusable PlannedStatement.
// No logical AST, metadata port, binder scope, or executor state is retained.
struct LoweredSelectPlan final {
    std::uint64_t catalog_generation{0U};
    Plan root;
    ParameterNamespace parameters;
};

// Finalizes the lowered SELECT product as the immutable, reusable statement
// unit. It consumes the physical tree and namespace, performs their one
// completed-plan validation, and retains no metadata port or logical AST.
auto assemble_planned_select(LoweredSelectPlan lowered) -> base::Result<PlannedStatement>;

// Statement-level planner composition boundary. SELECT and executable DML
// forms enter as one planner-owned variant, share one namespace and
// metadata generation, then leave as an immutable reusable statement.  It
// deliberately has no parser/binder dependency.
auto lower_preprocessed_statement(
    PreprocessedStatement statement,
    const MetadataPort& metadata_port,
    ParameterNamespace parameters
) -> base::Result<PlannedStatement>;

// Consumes a planner-owned SELECT query and statement-wide parameter namespace
// and translates them into an owned physical tree.  The metadata port is
// borrowed only for the duration of construction; every table/index read must
// agree with the returned catalog generation.  Passing the namespace by value
// makes failure atomic from the caller's perspective: executor parameters for
// a selected parameterized join never mutate a namespace the caller retains.
//
// Derived FROM items are lowered recursively into SubqueryScanPlan. Scalar,
// IN, and EXISTS expressions are lowered into the nearest SubPlanEvalPlan;
// correlations are rewritten into statement-wide executor parameters before
// physical construction. DML expression subqueries remain explicitly rejected
// until the modification executor owns their row-evaluation boundary.
auto lower_preprocessed_select(
    PreprocessedQuery query,
    const MetadataPort& metadata_port,
    ParameterNamespace parameters
) -> base::Result<LoweredSelectPlan>;

// This convenience boundary consumes the SQL-owned resolved query and the
// binder's copied parameter-slot description exactly once. It is intentionally
// the only facade that includes a BoundQuery in its implementation; the
// more reusable overload above remains the planner-to-planner composition API.
auto lower_bound_select(
    sql::BoundQuery query,
    std::span<const sql::BoundParameterSlot> parameter_slots,
    const MetadataPort& metadata_port
) -> base::Result<LoweredSelectPlan>;

// Thin SQL-bound convenience facade for executable statements and transparent
// EXPLAIN wrappers. It converts the one bound statement and copied external-
// slot description once, then delegates to lower_preprocessed_statement;
// physical lowering never retains a BoundStatement or binder lifetime. SQL
// EXPLAIN result-row dispatch remains an engine responsibility.
auto lower_bound_statement(
    sql::BoundStatement statement,
    std::span<const sql::BoundParameterSlot> parameter_slots,
    const MetadataPort& metadata_port
) -> base::Result<PlannedStatement>;

}  // namespace db::planner
