#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/sql/ast_common.hpp"
#include "db/types/coercion.hpp"
#include "db/types/data_type.hpp"
#include "db/types/operator.hpp"
#include "db/types/value.hpp"

namespace db::sql {

// The binder allocates range-table positions left-to-right within a query
// level. They are deliberately distinct from catalog OIDs and column offsets.
class BoundRangeTableIndex final {
public:
    constexpr explicit BoundRangeTableIndex(const std::uint32_t zero_based = 0U) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto operator==(const BoundRangeTableIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_{0U};
};

class BoundColumnIndex final {
public:
    constexpr explicit BoundColumnIndex(const std::uint32_t zero_based = 0U) noexcept
        : zero_based_{zero_based} {}

    constexpr auto zero_based() const noexcept -> std::uint32_t {
        return zero_based_;
    }

    constexpr auto operator==(const BoundColumnIndex&) const -> bool = default;

private:
    std::uint32_t zero_based_{0U};
};

struct BoundColumn final {
    std::string name;
    types::DataType data_type{types::kUnknownType};
};

struct BoundColumnReference final {
    BoundRangeTableIndex range_table_index;
    BoundColumnIndex column_index;
    types::DataType data_type{types::kUnknownType};
    std::uint32_t outer_query_levels{0U};
};

struct BoundParameter final {
    ParameterSlot slot{kInvalidParameterSlot};
    types::DataType data_type{types::kUnknownType};
};

struct BoundParameterSlot final {
    ParameterSlot slot{kInvalidParameterSlot};
    types::DataType data_type{types::kUnknownType};
    std::vector<SourceSpan> occurrences;
};

struct BoundExpression;
struct BoundQuery;
struct BoundTableReference;
struct BoundStatement;

struct BoundQueryDeleter final {
    constexpr BoundQueryDeleter() noexcept = default;

    constexpr BoundQueryDeleter(
        [[maybe_unused]] const std::default_delete<BoundQuery>& source_deleter
    ) noexcept {}

    void operator()(BoundQuery* query) const noexcept;
};

using BoundExpressionPtr = std::unique_ptr<BoundExpression>;
using BoundQueryPtr = std::unique_ptr<BoundQuery, BoundQueryDeleter>;
using BoundTableReferencePtr = std::unique_ptr<BoundTableReference>;
using BoundStatementPtr = std::unique_ptr<BoundStatement>;

struct BoundLiteralExpression final {
    types::Value value{types::Value::null(types::kUnknownType)};
};

struct BoundUnaryExpression final {
    types::UnaryOperator op{types::UnaryOperator::kPlus};
    BoundExpressionPtr operand;
};

struct BoundBinaryExpression final {
    types::BinaryOperator op{types::BinaryOperator::kEqual};
    BoundExpressionPtr left;
    BoundExpressionPtr right;
};

struct BoundBooleanExpression final {
    types::BooleanOperator op{types::BooleanOperator::kAnd};
    std::vector<BoundExpressionPtr> operands;
};

struct BoundIsNullExpression final {
    bool negated{false};
    BoundExpressionPtr operand;
};

struct BoundInListExpression final {
    bool negated{false};
    BoundExpressionPtr operand;
    std::vector<BoundExpressionPtr> values;
};

struct BoundBetweenExpression final {
    bool negated{false};
    BoundExpressionPtr operand;
    BoundExpressionPtr lower;
    BoundExpressionPtr upper;
};

// A cast can come from SQL spelling or be inserted by binder-selected coercion.
// Keeping the distinction makes the bound tree sufficient for later SQL
// rendering and prevents a planner from mistaking an implicit adaptation for
// user-written syntax.
enum class BoundCastForm : std::uint8_t {
    kImplicit,
    kExplicit,
};

struct BoundCastExpression final {
    BoundExpressionPtr operand;
    types::DataType target_type{types::kUnknownType};
    types::CoercionPath path{};
    BoundCastForm form{BoundCastForm::kImplicit};
};

enum class BoundAggregateKind : std::uint8_t {
    kCount,
    kSum,
    kAverage,
    kMinimum,
    kMaximum,
};

struct BoundAggregateExpression final {
    BoundAggregateKind kind{BoundAggregateKind::kCount};
    bool distinct{false};
    bool star{false};
    std::vector<BoundExpressionPtr> arguments;
    std::uint32_t aggregate_index{0U};
};

struct BoundScalarSubqueryExpression final {
    BoundQueryPtr query;
};

struct BoundExistsExpression final {
    bool negated{false};
    BoundQueryPtr query;
};

struct BoundInSubqueryExpression final {
    bool negated{false};
    BoundExpressionPtr operand;
    BoundQueryPtr query;
};

// Every bound expression has a resolved result type. Source expressions never
// carry this field because their type can depend on the surrounding catalog
// scope and coercion context.
struct BoundExpression final {
    using Node = std::variant<
        BoundLiteralExpression,
        BoundColumnReference,
        BoundParameter,
        BoundUnaryExpression,
        BoundBinaryExpression,
        BoundBooleanExpression,
        BoundIsNullExpression,
        BoundInListExpression,
        BoundBetweenExpression,
        BoundCastExpression,
        BoundAggregateExpression,
        BoundScalarSubqueryExpression,
        BoundExistsExpression,
        BoundInSubqueryExpression>;

    BoundExpression() = default;

    BoundExpression(SourceSpan source_span, types::DataType result_type, Node bound_node)
        : span{source_span},
          data_type{result_type},
          node{std::move(bound_node)} {}

    ~BoundExpression();
    BoundExpression(const BoundExpression&) = delete;
    auto operator=(const BoundExpression&) -> BoundExpression& = delete;
    BoundExpression(BoundExpression&&) noexcept = default;
    auto operator=(BoundExpression&&) noexcept -> BoundExpression& = default;

    SourceSpan span{};
    types::DataType data_type{types::kUnknownType};
    Node node{};
};

struct BoundProjectionItem final {
    BoundExpressionPtr expression;
    // This is the explicit SQL alias only.  Result-column naming for an
    // unaliased expression is a later query/output concern; deriving it here
    // would lose the distinction between `SELECT a` and `SELECT a AS a`.
    std::optional<std::string> alias;
};

struct BoundSortItem final {
    BoundExpressionPtr expression;
    SortDirection direction{SortDirection::kDefault};
    NullOrder null_order{NullOrder::kDefault};
};

struct BoundBaseTable final {
    base::Oid oid{base::kInvalidOid};
    std::string name;
    std::optional<std::string> alias;
    std::vector<BoundColumn> columns;
    BoundRangeTableIndex range_table_index;
};

struct BoundJoinOnCondition final {
    BoundExpressionPtr expression;
};

struct BoundJoinUsingCondition final {
    // The supported join form accepts exactly one source USING name. The
    // comparison is kept as a fully resolved expression so planning handles
    // ON and USING through the same typed predicate path while retaining the
    // original spelling for diagnostics/output shaping.
    std::string column_name;
    BoundExpressionPtr comparison;
};

struct BoundJoinTable final {
    using Condition = std::variant<std::monostate, BoundJoinOnCondition, BoundJoinUsingCondition>;

    JoinKind kind{JoinKind::kInner};
    BoundTableReferencePtr left;
    BoundTableReferencePtr right;
    Condition condition{};
};

struct BoundDerivedTable final {
    std::string alias;
    std::vector<BoundColumn> columns;
    BoundRangeTableIndex range_table_index;
    BoundQueryPtr query;
};

// A set-operation relation owns independently scoped query inputs. A SELECT
// wrapper supplies its output projection and complete-set ordering/window.
struct BoundUnionTable final {
    bool all{false};
    std::vector<BoundColumn> columns;
    BoundRangeTableIndex range_table_index;
    BoundQueryPtr left;
    BoundQueryPtr right;
};

struct BoundTableReference final {
    using Node = std::
        variant<std::monostate, BoundBaseTable, BoundJoinTable, BoundDerivedTable, BoundUnionTable>;

    BoundTableReference() = default;

    BoundTableReference(SourceSpan source_span, Node bound_node)
        : span{source_span},
          node{std::move(bound_node)} {}

    ~BoundTableReference();
    BoundTableReference(const BoundTableReference&) = delete;
    auto operator=(const BoundTableReference&) -> BoundTableReference& = delete;
    BoundTableReference(BoundTableReference&&) noexcept = default;
    auto operator=(BoundTableReference&&) noexcept -> BoundTableReference& = default;

    SourceSpan span{};
    Node node{};
};

struct BoundSelectQuery final {
    bool distinct{false};
    std::vector<BoundProjectionItem> projection;
    BoundTableReferencePtr from;
    BoundExpressionPtr filter;
    std::vector<BoundExpressionPtr> group_by;
    BoundExpressionPtr having;
    std::vector<BoundSortItem> order_by;
    BoundExpressionPtr limit;
    BoundExpressionPtr offset;
};

struct BoundQuery final {
    BoundQuery() = default;

    BoundQuery(SourceSpan source_span, BoundSelectQuery bound_select)
        : span{source_span},
          select{std::move(bound_select)} {}

    ~BoundQuery();
    BoundQuery(const BoundQuery&) = delete;
    auto operator=(const BoundQuery&) -> BoundQuery& = delete;
    BoundQuery(BoundQuery&&) noexcept = default;
    auto operator=(BoundQuery&&) noexcept -> BoundQuery& = default;

    SourceSpan span{};
    BoundSelectQuery select;
};

struct BoundInsertValues final {
    std::vector<std::vector<BoundExpressionPtr>> rows;
};

struct BoundInsertQuery final {
    BoundQueryPtr query;
};

using BoundInsertSource = std::variant<BoundInsertValues, BoundInsertQuery>;

struct BoundInsertStatement final {
    // The fully resolved target is owned by the bound statement so every DML
    // consumer shares one table identity, schema, and range-table position.
    // It does not retain the CatalogReadPort used during binding.
    BoundBaseTable target;
    std::vector<BoundColumnIndex> columns;
    BoundInsertSource source{};
};

struct BoundAssignment final {
    BoundColumnIndex column_index;
    BoundExpressionPtr value;
};

struct BoundUpdateStatement final {
    BoundBaseTable target;
    std::vector<BoundAssignment> assignments;
    BoundExpressionPtr filter;
};

struct BoundDeleteStatement final {
    BoundBaseTable target;
    BoundExpressionPtr filter;
};

struct BoundColumnDefinition final {
    std::string name;
    types::DataType data_type{types::kUnknownType};
};

struct BoundCreateTableStatement final {
    std::string table_name;
    std::vector<BoundColumnDefinition> columns;
    // This SQL-layer default intentionally mirrors the catalog's validated
    // heap default without making the frontend depend on the catalog target.
    std::uint8_t fillfactor{80U};
    bool if_not_exists{false};
};

struct BoundDropTableStatement final {
    std::vector<std::string> table_names;
    bool if_exists{false};
};

struct BoundCreateIndexStatement final {
    std::string name;
    base::Oid table_oid{base::kInvalidOid};
    std::vector<BoundColumnIndex> columns;
    bool unique{false};
    bool if_not_exists{false};
};

struct BoundDropIndexStatement final {
    std::vector<std::string> index_names;
    bool if_exists{false};
};

struct BoundTransactionStatement final {
    TransactionKind kind{TransactionKind::kBegin};
};

struct BoundSetTransactionStatement final {
    IsolationLevel isolation{IsolationLevel::kSerializable};
};

struct BoundExplainStatement final {
    BoundStatementPtr statement;
};

// COPY is bound only for the engine's streaming ingestion boundary.  The
// bound node deliberately stores the same owned table/column descriptors as
// DML and keeps the supported format narrow: text input with explicit
// delimiter and NULL marker options.  No parser arena, catalog port, or
// physical relation handle escapes into this tree.
struct BoundCopyFromStatement final {
    BoundBaseTable target;
    std::vector<BoundColumnIndex> columns;
    std::uint8_t delimiter{static_cast<std::uint8_t>('\t')};
    std::string null_marker{"\\N"};
};

struct BoundShowTablesStatement final {};

struct BoundVacuumStatement final {
    std::optional<base::Oid> table_oid;
};

struct BoundAnalyzeStatement final {
    std::optional<base::Oid> table_oid;
};

struct BoundCheckpointStatement final {
    bool is_static{false};
};

struct BoundStatement final {
    using Node = std::variant<
        BoundSelectQuery,
        BoundInsertStatement,
        BoundUpdateStatement,
        BoundDeleteStatement,
        BoundCreateTableStatement,
        BoundDropTableStatement,
        BoundCreateIndexStatement,
        BoundDropIndexStatement,
        BoundTransactionStatement,
        BoundSetTransactionStatement,
        BoundExplainStatement,
        BoundCopyFromStatement,
        BoundShowTablesStatement,
        BoundVacuumStatement,
        BoundAnalyzeStatement,
        BoundCheckpointStatement>;

    BoundStatement() = default;

    BoundStatement(SourceSpan source_span, Node bound_node)
        : span{source_span},
          node{std::move(bound_node)} {}

    ~BoundStatement();
    BoundStatement(const BoundStatement&) = delete;
    auto operator=(const BoundStatement&) -> BoundStatement& = delete;
    BoundStatement(BoundStatement&&) noexcept = default;
    auto operator=(BoundStatement&&) noexcept -> BoundStatement& = default;

    SourceSpan span{};
    Node node{};
};

// BoundScript owns a catalog-independent snapshot of all binding results.
// It contains copied table descriptors rather than a CatalogReadPort reference,
// so later planner/executor work cannot retain an invalid catalog view.
struct BoundScript final {
    BoundScript() = default;
    BoundScript(const BoundScript&) = delete;
    auto operator=(const BoundScript&) -> BoundScript& = delete;
    BoundScript(BoundScript&&) noexcept = default;
    auto operator=(BoundScript&&) noexcept -> BoundScript& = default;
    ~BoundScript() = default;

    // Captured solely as an observable provenance value. Bound nodes copy all
    // descriptors they need and never use this to reach back into a catalog.
    std::uint64_t catalog_generation{0U};
    std::vector<BoundStatement> statements;
    std::vector<BoundParameterSlot> parameter_slots;
};

inline BoundExpression::~BoundExpression() = default;
inline BoundTableReference::~BoundTableReference() = default;
inline BoundQuery::~BoundQuery() = default;
inline BoundStatement::~BoundStatement() = default;

inline void BoundQueryDeleter::operator()(BoundQuery* query) const noexcept {
    // std::unique_ptr transfers its owned object to this deleter protocol.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete query;
}

}  // namespace db::sql
