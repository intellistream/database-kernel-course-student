#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/sql/ast_common.hpp"
#include "db/types/coercion.hpp"
#include "db/types/data_type.hpp"
#include "db/types/operator.hpp"
#include "db/types/value.hpp"

namespace db::sql {
struct BoundQuery;
struct BoundStatement;
}  // namespace db::sql

namespace db::planner {

// Logical range-table and column positions are intentionally different from
// both sql::Bound* positions and physical PlanColumnIndex values.  They are
// owned by the preplanning IR while join simplification and path construction
// still need relation identity; physical lowering later maps references to
// operator input positions.
class QueryRangeTableIndex final {
public:
    constexpr explicit QueryRangeTableIndex(const std::uint32_t zero_based = 0U) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto operator==(const QueryRangeTableIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_{0U};
};

class QueryColumnIndex final {
public:
    constexpr explicit QueryColumnIndex(const std::uint32_t zero_based = 0U) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto operator==(const QueryColumnIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_{0U};
};

class QueryAggregateIndex final {
public:
    constexpr explicit QueryAggregateIndex(const std::uint32_t zero_based = 0U) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto operator==(const QueryAggregateIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_{0U};
};

// Executor parameters are allocated while lowering correlated subqueries.
// This logical spelling is deliberately distinct from sql::ParameterSlot:
// external `$n` parameters originate in the SQL contract, while these IDs
// are statement-local planner products and are never exposed to the binder.
class QueryExecutorParameterId final {
public:
    constexpr explicit QueryExecutorParameterId(
        const std::uint32_t zero_based = std::numeric_limits<std::uint32_t>::max()
    ) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto valid() const noexcept -> bool {
        return zero_based_ != std::numeric_limits<std::uint32_t>::max();
    }

    constexpr auto operator==(const QueryExecutorParameterId&) const -> bool = default;

private:
    std::uint32_t zero_based_{std::numeric_limits<std::uint32_t>::max()};
};

struct PreprocessedExpression;
struct PreprocessedQuery;
struct PreprocessedTableReference;

struct PreprocessedQueryDeleter final {
    constexpr PreprocessedQueryDeleter() noexcept = default;

    constexpr PreprocessedQueryDeleter(
        [[maybe_unused]] const std::default_delete<PreprocessedQuery>& source_deleter
    ) noexcept {}

    void operator()(PreprocessedQuery* query) const noexcept;
};

using PreprocessedExpressionPtr = std::unique_ptr<PreprocessedExpression>;
using PreprocessedQueryPtr = std::unique_ptr<PreprocessedQuery, PreprocessedQueryDeleter>;
using PreprocessedTableReferencePtr = std::unique_ptr<PreprocessedTableReference>;

struct QueryColumn final {
    std::string name;
    types::DataType data_type{types::kUnknownType};

    auto operator==(const QueryColumn&) const -> bool = default;
};

struct QueryLiteralExpression final {
    types::Value value{types::Value::null(types::kUnknownType)};
};

struct QueryColumnReference final {
    QueryRangeTableIndex range_table_index;
    QueryColumnIndex column_index;
    types::DataType data_type{types::kUnknownType};
    std::uint32_t outer_query_levels{0U};

    auto operator==(const QueryColumnReference&) const -> bool = default;
};

struct QueryParameterReference final {
    sql::ParameterSlot slot{sql::kInvalidParameterSlot};
    types::DataType data_type{types::kUnknownType};

    auto operator==(const QueryParameterReference&) const -> bool = default;
};

struct QueryExecutorParameterReference final {
    QueryExecutorParameterId id{};
    types::DataType data_type{types::kUnknownType};

    auto operator==(const QueryExecutorParameterReference&) const -> bool = default;
};

struct QueryUnaryExpression final {
    types::UnaryOperator op{types::UnaryOperator::kPlus};
    PreprocessedExpressionPtr operand;
};

struct QueryBinaryExpression final {
    types::BinaryOperator op{types::BinaryOperator::kEqual};
    PreprocessedExpressionPtr left;
    PreprocessedExpressionPtr right;
};

struct QueryBooleanExpression final {
    types::BooleanOperator op{types::BooleanOperator::kAnd};
    std::vector<PreprocessedExpressionPtr> operands;
};

struct QueryIsNullExpression final {
    bool negated{false};
    PreprocessedExpressionPtr operand;
};

struct QueryInListExpression final {
    bool negated{false};
    PreprocessedExpressionPtr operand;
    std::vector<PreprocessedExpressionPtr> values;
};

struct QueryBetweenExpression final {
    bool negated{false};
    PreprocessedExpressionPtr operand;
    PreprocessedExpressionPtr lower;
    PreprocessedExpressionPtr upper;
};

enum class QueryCastForm : std::uint8_t {
    kImplicit,
    kExplicit,
};

struct QueryCastExpression final {
    PreprocessedExpressionPtr operand;
    types::DataType target_type{types::kUnknownType};
    types::CoercionPath path{};
    QueryCastForm form{QueryCastForm::kImplicit};
};

enum class QueryAggregateKind : std::uint8_t {
    kCount,
    kSum,
    kAverage,
    kMinimum,
    kMaximum,
};

struct QueryAggregateExpression final {
    QueryAggregateKind kind{QueryAggregateKind::kCount};
    bool distinct{false};
    bool star{false};
    std::vector<PreprocessedExpressionPtr> arguments;
    QueryAggregateIndex aggregate_index{};
};

struct QueryScalarSubqueryExpression final {
    PreprocessedQueryPtr query;
};

struct QueryExistsExpression final {
    bool negated{false};
    PreprocessedQueryPtr query;
};

struct QueryInSubqueryExpression final {
    bool negated{false};
    PreprocessedExpressionPtr operand;
    PreprocessedQueryPtr query;
};

// This is a planner-owned, resolved logical expression.  It deliberately
// retains range-table references until physical input-position assignment,
// while no node retains a binder scope, parser allocation, or catalog port.
struct PreprocessedExpression final {
    using Node = std::variant<
        QueryLiteralExpression,
        QueryColumnReference,
        QueryParameterReference,
        QueryExecutorParameterReference,
        QueryUnaryExpression,
        QueryBinaryExpression,
        QueryBooleanExpression,
        QueryIsNullExpression,
        QueryInListExpression,
        QueryBetweenExpression,
        QueryCastExpression,
        QueryAggregateExpression,
        QueryScalarSubqueryExpression,
        QueryExistsExpression,
        QueryInSubqueryExpression>;

    PreprocessedExpression() = delete;

    PreprocessedExpression(
        sql::SourceSpan source_span,
        types::DataType result_type,
        Node query_node
    )
        : span{source_span},
          data_type{result_type},
          node{std::move(query_node)} {}

    ~PreprocessedExpression();
    PreprocessedExpression(const PreprocessedExpression&) = delete;
    auto operator=(const PreprocessedExpression&) -> PreprocessedExpression& = delete;
    PreprocessedExpression(PreprocessedExpression&&) noexcept = default;
    auto operator=(PreprocessedExpression&&) noexcept -> PreprocessedExpression& = default;

    sql::SourceSpan span{};
    types::DataType data_type{types::kUnknownType};
    Node node;
};

struct PreprocessedProjectionItem final {
    PreprocessedExpressionPtr expression;
    std::optional<std::string> alias;
};

struct PreprocessedSortItem final {
    PreprocessedExpressionPtr expression;
    sql::SortDirection direction{sql::SortDirection::kDefault};
    sql::NullOrder null_order{sql::NullOrder::kDefault};
};

struct PreprocessedBaseTable final {
    base::Oid oid{base::kInvalidOid};
    std::string name;
    std::optional<std::string> alias;
    std::vector<QueryColumn> columns;
    QueryRangeTableIndex range_table_index;
};

struct PreprocessedJoinOnCondition final {
    PreprocessedExpressionPtr expression;
};

struct PreprocessedJoinUsingCondition final {
    std::string column_name;
    PreprocessedExpressionPtr comparison;
};

struct PreprocessedJoinTable final {
    using Condition =
        std::variant<std::monostate, PreprocessedJoinOnCondition, PreprocessedJoinUsingCondition>;

    sql::JoinKind kind{sql::JoinKind::kInner};
    PreprocessedTableReferencePtr left;
    PreprocessedTableReferencePtr right;
    Condition condition{};
};

struct PreprocessedDerivedTable final {
    std::string alias;
    std::vector<QueryColumn> columns;
    QueryRangeTableIndex range_table_index;
    PreprocessedQueryPtr query;
};

struct PreprocessedUnionTable final {
    bool all{false};
    std::vector<QueryColumn> columns;
    QueryRangeTableIndex range_table_index;
    PreprocessedQueryPtr left;
    PreprocessedQueryPtr right;
};

// A query level represents no FROM item with a null pointer.  A present table
// reference never has an empty node, which avoids making a malformed table
// sentinel look like a valid constant-query source.
struct PreprocessedTableReference final {
    using Node = std::variant<
        PreprocessedBaseTable,
        PreprocessedJoinTable,
        PreprocessedDerivedTable,
        PreprocessedUnionTable>;

    PreprocessedTableReference() = delete;

    PreprocessedTableReference(sql::SourceSpan source_span, Node query_node)
        : span{source_span},
          node{std::move(query_node)} {}

    ~PreprocessedTableReference();
    PreprocessedTableReference(const PreprocessedTableReference&) = delete;
    auto operator=(const PreprocessedTableReference&) -> PreprocessedTableReference& = delete;
    PreprocessedTableReference(PreprocessedTableReference&&) noexcept = default;
    auto operator=(PreprocessedTableReference&&) noexcept -> PreprocessedTableReference& = default;

    sql::SourceSpan span{};
    Node node;
};

struct PreprocessedSelectQuery final {
    bool distinct{false};
    std::vector<PreprocessedProjectionItem> projection;
    PreprocessedTableReferencePtr from;
    PreprocessedExpressionPtr filter;
    std::vector<PreprocessedExpressionPtr> group_by;
    PreprocessedExpressionPtr having;
    std::vector<PreprocessedSortItem> order_by;
    PreprocessedExpressionPtr limit;
    PreprocessedExpressionPtr offset;
};

// Preprocessing takes ownership of a BoundQuery and converts it once into this
// independent form. Later planner stages share this result without re-entering
// SQL binder implementation details or cloning its AST.
struct PreprocessedQuery final {
    PreprocessedQuery() = delete;

    PreprocessedQuery(sql::SourceSpan source_span, PreprocessedSelectQuery query_select)
        : span{source_span},
          select{std::move(query_select)} {}

    ~PreprocessedQuery();
    PreprocessedQuery(const PreprocessedQuery&) = delete;
    auto operator=(const PreprocessedQuery&) -> PreprocessedQuery& = delete;
    PreprocessedQuery(PreprocessedQuery&&) noexcept = default;
    auto operator=(PreprocessedQuery&&) noexcept -> PreprocessedQuery& = default;

    sql::SourceSpan span{};
    PreprocessedSelectQuery select;
};

// DML uses the same resolved base-table and expression DTOs as SELECT rather
// than copying SQL-layer fields into a second planner vocabulary.  The target
// always has range-table index zero; keeping that identity here lets UPDATE
// and DELETE reuse normal base-path lowering without retaining a binder scope.
struct PreprocessedInsertValues final {
    std::vector<std::vector<PreprocessedExpressionPtr>> rows;
};

struct PreprocessedInsertQuery final {
    PreprocessedQueryPtr query;
};

using PreprocessedInsertSource = std::variant<PreprocessedInsertValues, PreprocessedInsertQuery>;

struct PreprocessedInsertStatement final {
    PreprocessedBaseTable target;
    std::vector<QueryColumnIndex> columns;
    PreprocessedInsertSource source{};
};

struct PreprocessedAssignment final {
    QueryColumnIndex column_index;
    PreprocessedExpressionPtr value;
};

struct PreprocessedUpdateStatement final {
    PreprocessedBaseTable target;
    std::vector<PreprocessedAssignment> assignments;
    PreprocessedExpressionPtr filter;
};

struct PreprocessedDeleteStatement final {
    PreprocessedBaseTable target;
    PreprocessedExpressionPtr filter;
};

// The planner-owned statement boundary deliberately accepts only executable
// data statements. Utility, transaction, DDL, and COPY dispatch remain
// composition/engine work. `lower_bound_statement` unwraps an EXPLAIN wrapper
// before this conversion, so the logical and physical layers never need a SQL
// utility node or an EXPLAIN result-row representation.
using PreprocessedStatement = std::variant<
    PreprocessedQuery,
    PreprocessedInsertStatement,
    PreprocessedUpdateStatement,
    PreprocessedDeleteStatement>;

// Converts and consumes the SQL-owned bound tree, validates executable query
// shape, folds qualification constants, canonicalizes safe qualifiers, and
// pulls up only semantically transparent derived tables.
auto preprocess_bound_query(sql::BoundQuery query) -> base::Result<PreprocessedQuery>;

// Consumes one resolved SELECT/INSERT/UPDATE/DELETE statement and immediately
// replaces its SQL-owned AST with this planner-owned value.  This is the
// statement-level counterpart to preprocess_bound_query; all physical
// lowering remains independent of sql::Bound* definitions.
auto preprocess_bound_statement(sql::BoundStatement statement)
    -> base::Result<PreprocessedStatement>;

// Constant folding is separately public so future projection/DML callers can
// reuse the checked, SQL-three-valued implementation without invoking join or
// query-level rewrites.  It never duplicates a non-constant expression.
auto fold_constant_expression(PreprocessedExpression expression)
    -> base::Result<PreprocessedExpression>;

// Qualification preprocessing first folds constants, then flattens AND/OR and
// factors only structurally equal total leaves or comparisons over those
// leaves. Unlike DB's display-string comparison this is independent of
// diagnostic rendering and does not move a potentially erroring expression.
auto preprocess_qualification(PreprocessedExpression expression)
    -> base::Result<PreprocessedExpression>;

auto structurally_equal(const PreprocessedExpression& left, const PreprocessedExpression& right)
    -> bool;
auto expression_has_aggregate(const PreprocessedExpression& expression) noexcept -> bool;
auto expression_has_subquery(const PreprocessedExpression& expression) noexcept -> bool;

// Validates one standalone planner-owned expression without requiring a
// synthetic query wrapper.  Path, projection, and future DML lowering use it
// before they consume a public logical-IR value supplied by another planner
// stage.
auto validate_preprocessed_expression(const PreprocessedExpression& expression) -> base::Status;

// Rejects represented SQL shapes which do not yet have a correct physical
// execution path.  It is independent from parser/binder validation and is
// reusable by statement planners before plan construction.
auto validate_query_safety(const PreprocessedQuery& query) -> base::Status;

// Pulls up a deliberately narrow derived-table subset in place.  Only a
// single-base-table identity projection with no scope-crossing references is
// rewritten; all other derived tables remain explicit for physical lowering.
auto simplify_jointree(PreprocessedQuery& query) -> base::Status;

inline void PreprocessedQueryDeleter::operator()(PreprocessedQuery* query) const noexcept {
    // std::unique_ptr transfers its owned object to this deleter protocol.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete query;
}

}  // namespace db::planner
