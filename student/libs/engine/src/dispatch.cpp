#include "db/engine/dispatch.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/base/lab_missing.hpp"
#include "db/catalog/snapshot.hpp"
#include "db/catalog/store.hpp"
#include "db/engine/database.hpp"
#include "db/engine/session.hpp"
#include "db/planner/lowering.hpp"
#include "db/planner/metadata_port.hpp"
#include "db/sql/binder.hpp"
#include "db/sql/bound_ast.hpp"
#include "db/sql/catalog_port.hpp"
#include "db/sql/parser.hpp"
#include "db/types/type_io.hpp"
#include "db/types/type_registry.hpp"

#include "catalog_snapshot_adapter.hpp"
#include "command_scope.hpp"
#include "dispatch_internal.hpp"

namespace db::engine {
namespace {

using detail::CatalogSnapshotAdapter;

inline constexpr base::ExceptionBoundaryPolicy kExecutionPortExceptionPolicy{
    .component = "engine.dispatch",
    .allocation_message = "dispatch execution port exhausted available memory",
    .exception_prefix = "dispatch execution port threw an exception: ",
    .unknown_exception_message = "dispatch execution port threw a non-standard exception",
};

inline constexpr base::ExceptionBoundaryPolicy kPlanStateExceptionPolicy{
    .component = "engine.dispatch",
    .allocation_message = "dispatch plan state exhausted available memory",
    .exception_prefix = "dispatch plan state construction threw an exception: ",
    .unknown_exception_message = "dispatch plan state construction threw a non-standard exception",
};

[[maybe_unused]] auto
dispatch_error(const base::ErrorCode code, std::string message, std::string operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "engine.dispatch",
        std::move(operation)
    );
}

[[maybe_unused]] auto stale_catalog(
    [[maybe_unused]] const std::uint64_t expected,
    [[maybe_unused]] const std::uint64_t actual
) -> base::Error {
    // LAB-S02.Q11:R21-BEGIN
    base::lab::missing_algorithm("S02.Q11:R21");
    // LAB-S02.Q11:R21-END
}

[[maybe_unused]] auto type_name(const types::DataType data_type) -> std::string {
    return std::string{types::type_name(data_type.id()).value_or("unknown")};
}

struct PreparedParameterValues final {
    std::span<const types::Value> borrowed;
    std::vector<types::Value> owned;

    [[maybe_unused]] auto values() const noexcept -> std::span<const types::Value> {
        // LAB-S02.Q11:R22-BEGIN
        base::lab::missing_algorithm("S02.Q11:R22");
        // LAB-S02.Q11:R22-END
    }
};

[[maybe_unused]] auto prepare_parameter_values(
    [[maybe_unused]] const std::span<const types::DataType> expected,
    [[maybe_unused]] const std::span<const types::Value> actual
) -> base::Result<PreparedParameterValues> {
    // LAB-S02.Q11:R23-BEGIN
    base::lab::missing_algorithm("S02.Q11:R23");
    // LAB-S02.Q11:R23-END
}

auto statement_kind(const sql::BoundStatement& statement) -> base::Result<DispatchStatementKind>;

[[maybe_unused]] auto
result_schema_for_plan(const planner::PlannedStatement& planned, const DispatchStatementKind kind)
    -> base::Result<ResultSchema> {
    if (kind != DispatchStatementKind::kQuery) {
        return ResultSchema::create({});
    }
    const auto& columns = planner::plan_metadata(planned.root()).output.columns;
    std::vector<ResultColumn> result_columns;
    result_columns.reserve(columns.size());
    for (const auto& column : columns) {
        result_columns.push_back(
            ResultColumn{
                .name = column.name,
                .data_type = column.data_type,
            }
        );
    }
    return ResultSchema::create(std::move(result_columns));
}

[[maybe_unused]] void append_execution_result(
    [[maybe_unused]] DispatchResult& result,
    [[maybe_unused]] DispatchExecutionResult executed
) {
    // LAB-S02.Q11:R24-BEGIN
    base::lab::missing_algorithm("S02.Q11:R24");
    // LAB-S02.Q11:R24-END
}

[[maybe_unused]] auto execute_at_port(
    [[maybe_unused]] DispatchExecutionPort& execution,
    [[maybe_unused]] const DispatchPlan& plan,
    [[maybe_unused]] const std::span<const types::Value> parameter_values,
    [[maybe_unused]] const std::string_view operation
) -> base::Result<DispatchExecutionResult> {
    // LAB-S02.Q11:R25-BEGIN
    base::lab::missing_algorithm("S02.Q11:R25");
    // LAB-S02.Q11:R25-END
}

struct StatementPlan final {
    DispatchStatementKind kind{DispatchStatementKind::kQuery};
    DispatchPlan plan;
};

[[maybe_unused]] auto plan_statement(
    [[maybe_unused]] sql::BoundStatement statement,
    [[maybe_unused]] const std::span<const sql::BoundParameterSlot> parameter_slots,
    [[maybe_unused]] CatalogSnapshotAdapter& adapter,
    [[maybe_unused]] const std::function<base::Result<std::uint64_t>()>& current_generation
) -> base::Result<StatementPlan> {
    // LAB-S02.Q11:R26-BEGIN
    base::lab::missing_algorithm("S02.Q11:R26");
    // LAB-S02.Q11:R26-END
}

[[maybe_unused]] auto statement_kind(const sql::BoundStatement& statement)
    -> base::Result<DispatchStatementKind> {
    return std::visit(
        [](const auto& node) -> base::Result<DispatchStatementKind> {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, sql::BoundSelectQuery>) {
                return DispatchStatementKind::kQuery;
            } else if constexpr (std::is_same_v<Node, sql::BoundInsertStatement>) {
                return DispatchStatementKind::kInsert;
            } else if constexpr (std::is_same_v<Node, sql::BoundUpdateStatement>) {
                return DispatchStatementKind::kUpdate;
            } else if constexpr (std::is_same_v<Node, sql::BoundDeleteStatement>) {
                return DispatchStatementKind::kDelete;
            } else if constexpr (
                std::is_same_v<Node, sql::BoundTransactionStatement>
                || std::is_same_v<Node, sql::BoundSetTransactionStatement>
            ) {
                return DispatchStatementKind::kTransaction;
            } else if constexpr (std::is_same_v<Node, sql::BoundExplainStatement>) {
                return DispatchStatementKind::kExplain;
            } else {
                return DispatchStatementKind::kUtility;
            }
        },
        statement.node
    );
}

[[maybe_unused]] auto execute_utility_statement(
    [[maybe_unused]] Session& session,
    [[maybe_unused]] sql::BoundStatement statement,
    [[maybe_unused]] CatalogSnapshotAdapter& adapter,
    [[maybe_unused]] const std::function<base::Result<std::uint64_t>()>& current_generation,
    [[maybe_unused]] const detail::UtilityDispatchHandler& utility
) -> base::Result<DispatchExecutionResult> {
    // LAB-S02.Q11:R27-BEGIN
    base::lab::missing_algorithm("S02.Q11:R27");
    // LAB-S02.Q11:R27-END
}
}  // namespace

struct DispatchPlan::Impl final {
    std::shared_ptr<const planner::PlannedStatement> statement;
    DispatchStatementKind statement_kind{DispatchStatementKind::kQuery};
    std::size_t parameter_count{0U};

    [[maybe_unused]] Impl(
        [[maybe_unused]] std::shared_ptr<const planner::PlannedStatement> planned,
        [[maybe_unused]] const DispatchStatementKind kind,
        [[maybe_unused]] const std::size_t parameters
    ) noexcept
        : statement{std::move(planned)},
          statement_kind{kind},
          parameter_count{parameters} {
        // LAB-S02.Q11:R28-BEGIN
        base::lab::missing_algorithm("S02.Q11:R28");
        // LAB-S02.Q11:R28-END
    }
};

[[maybe_unused]] DispatchPlan::DispatchPlan(
    [[maybe_unused]] std::unique_ptr<Impl> implementation
) noexcept
    : implementation_{std::move(implementation)} {
    // LAB-S02.Q11:R29-BEGIN
    base::lab::missing_algorithm("S02.Q11:R29");
    // LAB-S02.Q11:R29-END
}

DispatchPlan::DispatchPlan(DispatchPlan&& other) noexcept = default;
auto DispatchPlan::operator=(DispatchPlan&& other) noexcept -> DispatchPlan& = default;
DispatchPlan::~DispatchPlan() = default;

[[maybe_unused]] auto detail::DispatchFactory::make(
    [[maybe_unused]] planner::PlannedStatement statement,
    [[maybe_unused]] const DispatchStatementKind kind,
    [[maybe_unused]] const std::size_t parameter_count
) -> base::Result<DispatchPlan> {
    // LAB-S02.Q11:R30-BEGIN
    base::lab::missing_algorithm("S02.Q11:R30");
    // LAB-S02.Q11:R30-END
}

[[maybe_unused]] auto detail::DispatchFactory::make(
    [[maybe_unused]] std::shared_ptr<const planner::PlannedStatement> statement,
    [[maybe_unused]] const DispatchStatementKind kind,
    [[maybe_unused]] const std::size_t parameter_count
) -> base::Result<DispatchPlan> {
    // LAB-S02.Q11:R31-BEGIN
    base::lab::missing_algorithm("S02.Q11:R31");
    // LAB-S02.Q11:R31-END
}

struct PreparedStatement::Impl final {
    std::shared_ptr<const planner::PlannedStatement> statement;
    DispatchStatementKind statement_kind{DispatchStatementKind::kScript};
    std::vector<types::DataType> parameter_types;
    std::shared_ptr<const ResultSchema> result_schema;

    [[maybe_unused]] Impl(
        [[maybe_unused]] std::shared_ptr<const planner::PlannedStatement> planned,
        [[maybe_unused]] const DispatchStatementKind kind,
        [[maybe_unused]] std::vector<types::DataType> types,
        [[maybe_unused]] std::shared_ptr<const ResultSchema> schema
    ) noexcept
        : statement{std::move(planned)},
          statement_kind{kind},
          parameter_types{std::move(types)},
          result_schema{std::move(schema)} {
        // LAB-S02.Q11:R32-BEGIN
        base::lab::missing_algorithm("S02.Q11:R32");
        // LAB-S02.Q11:R32-END
    }
};

[[maybe_unused]] PreparedStatement::PreparedStatement(
    [[maybe_unused]] std::unique_ptr<Impl> implementation
) noexcept
    : implementation_{std::move(implementation)} {
    // LAB-S02.Q11:R33-BEGIN
    base::lab::missing_algorithm("S02.Q11:R33");
    // LAB-S02.Q11:R33-END
}

PreparedStatement::PreparedStatement(PreparedStatement&& other) noexcept = default;
auto PreparedStatement::operator=(PreparedStatement&& other) noexcept
    -> PreparedStatement& = default;
PreparedStatement::~PreparedStatement() = default;

auto PreparedStatement::valid() const noexcept -> bool {
    return implementation_ != nullptr && implementation_->statement != nullptr;
}

auto PreparedStatement::kind() const noexcept -> DispatchStatementKind {
    return !valid() ? DispatchStatementKind::kScript : implementation_->statement_kind;
}

[[maybe_unused]] auto PreparedStatement::catalog_generation() const noexcept -> std::uint64_t {
    // LAB-S02.Q11:R34-BEGIN
    base::lab::missing_algorithm("S02.Q11:R34");
    // LAB-S02.Q11:R34-END
}

auto PreparedStatement::parameter_count() const noexcept -> std::size_t {
    return !valid() ? 0U : implementation_->parameter_types.size();
}

auto PreparedStatement::parameter_types() const noexcept -> std::span<const types::DataType> {
    return !valid() ? std::span<const types::DataType>{}
                    : std::span<const types::DataType>{implementation_->parameter_types};
}

auto PreparedStatement::result_columns() const noexcept -> std::span<const PreparedColumn> {
    return !valid() || implementation_->result_schema == nullptr
               ? std::span<const PreparedColumn>{}
               : implementation_->result_schema->columns();
}

[[maybe_unused]] auto detail::PreparedFactory::make(
    [[maybe_unused]] planner::PlannedStatement statement,
    [[maybe_unused]] const DispatchStatementKind kind,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types,
    [[maybe_unused]] ResultSchema result_schema
) -> base::Result<PreparedStatement> {
    // LAB-S02.Q11:R35-BEGIN
    base::lab::missing_algorithm("S02.Q11:R35");
    // LAB-S02.Q11:R35-END
}

[[maybe_unused]] auto
detail::PreparedFactory::dispatch_plan([[maybe_unused]] const PreparedStatement& statement)
    -> base::Result<DispatchPlan> {
    // LAB-S02.Q11:R36-BEGIN
    base::lab::missing_algorithm("S02.Q11:R36");
    // LAB-S02.Q11:R36-END
}

auto DispatchPlan::kind() const noexcept -> DispatchStatementKind {
    return implementation_ == nullptr ? DispatchStatementKind::kScript
                                      : implementation_->statement_kind;
}

[[maybe_unused]] auto DispatchPlan::catalog_generation() const noexcept -> std::uint64_t {
    // LAB-S02.Q11:R37-BEGIN
    base::lab::missing_algorithm("S02.Q11:R37");
    // LAB-S02.Q11:R37-END
}

auto DispatchPlan::parameter_count() const noexcept -> std::size_t {
    return implementation_ == nullptr ? 0U : implementation_->parameter_count;
}

[[maybe_unused]] auto
detail::DispatchPlanAccess::statement([[maybe_unused]] const DispatchPlan& plan) noexcept
    -> const planner::PlannedStatement* {
    // LAB-S02.Q11:R38-BEGIN
    base::lab::missing_algorithm("S02.Q11:R38");
    // LAB-S02.Q11:R38-END
}

[[maybe_unused]] auto
detail::DispatchPlanAccess::statement([[maybe_unused]] const PreparedStatement& statement) noexcept
    -> const planner::PlannedStatement* {
    // LAB-S02.Q11:R39-BEGIN
    base::lab::missing_algorithm("S02.Q11:R39");
    // LAB-S02.Q11:R39-END
}

[[maybe_unused]] auto detail::dispatch_sql(
    [[maybe_unused]] Session& session,
    [[maybe_unused]] const std::string_view sql_text,
    [[maybe_unused]] DispatchExecutionPort& execution,
    [[maybe_unused]] const DispatchParameters parameters,
    [[maybe_unused]] catalog::CatalogSnapshot snapshot,
    [[maybe_unused]] const std::function<base::Result<std::uint64_t>()>& current_generation,
    [[maybe_unused]] const detail::UtilityDispatchHandler& utility
) -> base::Result<DispatchResult> {
    // LAB-S02.Q11:R40-BEGIN
    base::lab::missing_algorithm("S02.Q11:R40");
    // LAB-S02.Q11:R40-END
}

[[maybe_unused]] auto detail::prepare_sql(
    [[maybe_unused]] const std::string_view sql_text,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types,
    [[maybe_unused]] catalog::CatalogSnapshot snapshot,
    [[maybe_unused]] const std::function<base::Result<std::uint64_t>()>& current_generation
) -> base::Result<PreparedStatement> {
    // LAB-S02.Q11:R41-BEGIN
    base::lab::missing_algorithm("S02.Q11:R41");
    // LAB-S02.Q11:R41-END
}

[[maybe_unused]] auto detail::execute_prepared_sql(
    [[maybe_unused]] Session& session,
    [[maybe_unused]] const PreparedStatement& statement,
    [[maybe_unused]] DispatchExecutionPort& execution,
    [[maybe_unused]] const std::span<const types::Value> parameter_values,
    [[maybe_unused]] const std::function<base::Result<std::uint64_t>()>& current_generation
) -> base::Result<DispatchResult> {
    // LAB-S02.Q11:R42-BEGIN
    base::lab::missing_algorithm("S02.Q11:R42");
    // LAB-S02.Q11:R42-END
}
}  // namespace db::engine
