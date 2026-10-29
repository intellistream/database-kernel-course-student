#pragma once

#include "db/base/result.hpp"
#include "db/sql/bound_ast.hpp"

namespace db::sql::detail {

// Creates an independent move-only bound tree for the few binder paths that
// must reuse an already-bound expression (for example, ORDER BY aliases).
// Malformed input is rejected rather than partially cloned.
auto clone_bound_expression(const BoundExpression& source) -> base::Result<BoundExpressionPtr>;

}  // namespace db::sql::detail
