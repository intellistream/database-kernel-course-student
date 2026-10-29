#pragma once

#include "db/engine/dispatch.hpp"
#include "db/sql/bound_ast.hpp"

namespace db::engine::detail {

struct UtilityExecutionState;

// Runs synchronously within execute_utility's lock, catalog guard and command
// snapshot. Relation construction/removal and failed-build cleanup stay together.
auto execute_bound_utility(UtilityExecutionState& state, const sql::BoundCreateTableStatement& node)
    -> base::Result<DispatchExecutionResult>;
auto execute_bound_utility(UtilityExecutionState& state, const sql::BoundDropTableStatement& node)
    -> base::Result<DispatchExecutionResult>;
auto execute_bound_utility(UtilityExecutionState& state, const sql::BoundCreateIndexStatement& node)
    -> base::Result<DispatchExecutionResult>;
auto execute_bound_utility(UtilityExecutionState& state, const sql::BoundDropIndexStatement& node)
    -> base::Result<DispatchExecutionResult>;

}  // namespace db::engine::detail
