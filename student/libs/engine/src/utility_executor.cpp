#include "utility_executor.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/catalog/store.hpp"
#include "db/transaction/backend.hpp"

#include "ddl_executor.hpp"
#include "maintenance_executor.hpp"
#include "session_access.hpp"
#include "utility_execution_state.hpp"

namespace db::engine::detail {

auto execute_bound_utility(UtilityExecutionState& state, const sql::BoundShowTablesStatement& node)
    -> base::Result<DispatchExecutionResult> {
    static_cast<void>(node);
    auto tables = state.context.services.catalog.list_tables();
    if (!tables) {
        return base::fail(std::move(tables).error());
    }
    std::sort(tables->begin(), tables->end(), [](const auto& left, const auto& right) {
        return left.name() < right.name();
    });
    auto schema = ResultSchema::create({ResultColumn{
        .name = "table_name",
        .data_type = types::kCharType,
    }});
    if (!schema) {
        return base::fail(std::move(schema).error());
    }
    std::vector<std::vector<types::Value>> rows;
    rows.reserve(tables->size());
    for (const auto& table : *tables) {
        rows.push_back({types::Value::character(std::string{table.name()})});
    }
    auto result_set = ResultSet::create(std::move(schema).value(), std::move(rows));
    if (!result_set) {
        return base::fail(std::move(result_set).error());
    }
    return DispatchExecutionResult{
        .row_count = result_set->size(),
        .affected_row_count = 0U,
        .result_set = std::move(result_set).value(),
    };
}

auto execute_bound_utility(UtilityExecutionState& state, const sql::BoundCopyFromStatement& node)
    -> base::Result<DispatchExecutionResult> {
    static_cast<void>(state);
    static_cast<void>(node);
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "COPY FROM STDIN requires the Database::copy_from streaming API",
        }
    );
}

auto execute_bound_utility(UtilityExecutionState& state, const sql::BoundCheckpointStatement& node)
    -> base::Result<DispatchExecutionResult> {
    if (node.is_static) {
        return base::fail(
            base::Error{
                base::ErrorCode::kTransactionState,
                "CREATE STATIC_CHECKPOINT must be a standalone statement from an Idle session"
            }
        );
    }
    if (!state.context.checkpoint) {
        return base::fail(
            base::Error{
                base::ErrorCode::kUnavailable,
                "CHECKPOINT execution service is unavailable",
            }
        );
    }
    if (auto status = state.context.checkpoint(); !status) {
        return base::fail(
            std::move(status).error().with_context("engine.utility", "execute CHECKPOINT")
        );
    }
    return DispatchExecutionResult{};
}

template <typename Node>
auto execute_bound_utility(UtilityExecutionState& state, const Node& node)
    -> base::Result<DispatchExecutionResult> {
    static_cast<void>(state);
    static_cast<void>(node);
    return base::fail(
        base::Error{
            base::ErrorCode::kUnsupported,
            "statement is not supported by the utility execution boundary",
        }
    );
}

auto execute_utility(
    UtilityExecutionContext context,
    Session& session,
    sql::BoundStatement statement,
    const std::uint64_t expected_generation
) -> base::Result<DispatchExecutionResult> {
    const std::lock_guard utility_lock{context.utility_mutex};

    auto generation = context.services.catalog.generation();
    if (!generation) {
        return base::fail(std::move(generation).error());
    }
    if (generation.value() != expected_generation) {
        return base::fail(
            base::Error{
                base::ErrorCode::kUnavailable,
                "catalog generation changed before utility execution",
            }
                .with_context("engine.utility", "catalog guard")
        );
    }

    auto* backend = SessionAccess::backend(session);
    const auto* snapshot = SessionAccess::command_snapshot(session);
    if (backend == nullptr || snapshot == nullptr || !backend->command_active()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kTransactionState,
                "utility execution requires an active Session command",
            }
                .with_context("engine.utility", "command state")
        );
    }

    UtilityExecutionState state{context, *backend, *snapshot};
    return std::visit(
        [&](auto&& node) -> base::Result<DispatchExecutionResult> {
            return execute_bound_utility(state, node);
        },
        std::move(statement.node)
    );
}

}  // namespace db::engine::detail
