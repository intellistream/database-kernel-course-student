#pragma once

#include <string>

#include "db/base/result.hpp"
#include "db/planner/plan.hpp"

namespace db::planner {

// Renders deterministic PostgreSQL-style text for the public physical IR.
// It returns a structured plan error instead of dereferencing a malformed
// manually assembled tree. The engine owns EXPLAIN result integration.
auto render_explain(const Plan& plan) -> base::Result<std::string>;
auto render_explain(const PlannedStatement& statement) -> base::Result<std::string>;

}  // namespace db::planner
