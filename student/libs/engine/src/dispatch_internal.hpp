#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

#include "db/base/result.hpp"
#include "db/catalog/snapshot.hpp"
#include "db/engine/dispatch.hpp"
#include "db/sql/bound_ast.hpp"

namespace db::planner {
class PlannedStatement;
}  // namespace db::planner

namespace db::engine::detail {

class DispatchFactory final {
public:
    static auto make(
        planner::PlannedStatement statement,
        DispatchStatementKind kind,
        std::size_t parameter_count
    ) -> base::Result<DispatchPlan>;
    static auto make(
        std::shared_ptr<const planner::PlannedStatement> statement,
        DispatchStatementKind kind,
        std::size_t parameter_count
    ) -> base::Result<DispatchPlan>;
};

class PreparedFactory final {
public:
    static auto make(
        planner::PlannedStatement statement,
        DispatchStatementKind kind,
        std::span<const types::DataType> parameter_types,
        ResultSchema result_schema
    ) -> base::Result<PreparedStatement>;
    static auto dispatch_plan(const PreparedStatement& statement) -> base::Result<DispatchPlan>;
};

// The execution adapter lives at the composition boundary.  Keep planner
// ownership private while providing one narrow, read-only handoff for the
// engine's storage-backed adapter; callers receive no mutable plan or parser
// state.
class DispatchPlanAccess final {
public:
    static auto statement(const DispatchPlan& plan) noexcept -> const planner::PlannedStatement*;
    static auto statement(const PreparedStatement& statement) noexcept
        -> const planner::PlannedStatement*;
};

using UtilityDispatchHandler = std::function<base::Result<
    DispatchExecutionResult>(Session&, sql::BoundStatement, std::uint64_t expected_generation)>;

auto dispatch_sql(
    Session& session,
    std::string_view sql_text,
    DispatchExecutionPort& execution,
    DispatchParameters parameters,
    catalog::CatalogSnapshot snapshot,
    const std::function<base::Result<std::uint64_t>()>& current_generation,
    const UtilityDispatchHandler& utility
) -> base::Result<DispatchResult>;

auto prepare_sql(
    std::string_view sql_text,
    std::span<const types::DataType> parameter_types,
    catalog::CatalogSnapshot snapshot,
    const std::function<base::Result<std::uint64_t>()>& current_generation
) -> base::Result<PreparedStatement>;

auto execute_prepared_sql(
    Session& session,
    const PreparedStatement& statement,
    DispatchExecutionPort& execution,
    std::span<const types::Value> parameter_values,
    const std::function<base::Result<std::uint64_t>()>& current_generation
) -> base::Result<DispatchResult>;

}  // namespace db::engine::detail
