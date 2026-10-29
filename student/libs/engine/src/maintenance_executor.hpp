#pragma once

#include "db/engine/dispatch.hpp"
#include "db/sql/bound_ast.hpp"

namespace db::engine::detail {

struct UtilityExecutionState;

// Runs within execute_utility's catalog guard, command snapshot and utility lock.
auto execute_bound_utility(UtilityExecutionState& state, const sql::BoundAnalyzeStatement& node)
    -> base::Result<DispatchExecutionResult>;
auto execute_bound_utility(UtilityExecutionState& state, const sql::BoundVacuumStatement& node)
    -> base::Result<DispatchExecutionResult>;

}  // namespace db::engine::detail
