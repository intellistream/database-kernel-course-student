#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "db/sql/ast_common.hpp"

namespace db::sql {

struct SourceIdentifier final {
    SourceSpan span{};
    std::string spelling;
};

struct SourceQualifiedName final {
    SourceSpan span{};
    std::vector<SourceIdentifier> parts;
};

// The source AST retains type spelling and its optional numeric modifier.
// Binding maps it through db::types and validates the project type rules.
struct SourceTypeName final {
    SourceSpan span{};
    SourceQualifiedName name;
    std::optional<std::uint32_t> modifier;
};

enum class SourceLiteralKind : std::uint8_t {
    kNull,
    kBoolean,
    kInteger,
    kFloat,
    kString,
};

struct SourceExpression;
struct SourceQuery;
struct SourceTableReference;
struct SourceStatement;

struct SourceQueryDeleter final {
    constexpr SourceQueryDeleter() noexcept = default;

    constexpr SourceQueryDeleter(
        [[maybe_unused]] const std::default_delete<SourceQuery>& source_deleter
    ) noexcept {}

    void operator()(SourceQuery* query) const noexcept;
};

using SourceExpressionPtr = std::unique_ptr<SourceExpression>;
using SourceQueryPtr = std::unique_ptr<SourceQuery, SourceQueryDeleter>;
using SourceTableReferencePtr = std::unique_ptr<SourceTableReference>;
using SourceStatementPtr = std::unique_ptr<SourceStatement>;

struct SourceLiteralExpression final {
    SourceLiteralKind kind{SourceLiteralKind::kNull};
    std::string text;
};

struct SourceColumnReferenceExpression final {
    SourceQualifiedName name;
};

struct SourceParameterExpression final {
    ParameterSlot slot{kInvalidParameterSlot};
};

struct SourceUnaryExpression final {
    UnaryOperator op{UnaryOperator::kPlus};
    SourceExpressionPtr operand;
};

struct SourceBinaryExpression final {
    BinaryOperator op{BinaryOperator::kEqual};
    SourceExpressionPtr left;
    SourceExpressionPtr right;
};

struct SourceBooleanExpression final {
    BooleanOperator op{BooleanOperator::kAnd};
    std::vector<SourceExpressionPtr> operands;
};

struct SourceIsNullExpression final {
    bool negated{false};
    SourceExpressionPtr operand;
};

struct SourceInListExpression final {
    bool negated{false};
    SourceExpressionPtr operand;
    std::vector<SourceExpressionPtr> values;
};

struct SourceBetweenExpression final {
    bool negated{false};
    SourceExpressionPtr operand;
    SourceExpressionPtr lower;
    SourceExpressionPtr upper;
};

struct SourceCastExpression final {
    SourceExpressionPtr operand;
    SourceTypeName target_type;
};

struct SourceFunctionCallExpression final {
    SourceQualifiedName name;
    bool distinct{false};
    bool star{false};
    std::vector<SourceExpressionPtr> arguments;
};

struct SourceScalarSubqueryExpression final {
    SourceQueryPtr query;
};

struct SourceExistsExpression final {
    bool negated{false};
    SourceQueryPtr query;
};

struct SourceInSubqueryExpression final {
    bool negated{false};
    SourceExpressionPtr operand;
    SourceQueryPtr query;
};

// All source expression leaves and descendants are owned by SourceScript.
// A node has no reference to DuckDB raw-memory after parse_sql returns.
struct SourceExpression final {
    using Node = std::variant<
        SourceLiteralExpression,
        SourceColumnReferenceExpression,
        SourceParameterExpression,
        SourceUnaryExpression,
        SourceBinaryExpression,
        SourceBooleanExpression,
        SourceIsNullExpression,
        SourceInListExpression,
        SourceBetweenExpression,
        SourceCastExpression,
        SourceFunctionCallExpression,
        SourceScalarSubqueryExpression,
        SourceExistsExpression,
        SourceInSubqueryExpression>;

    SourceExpression() = default;

    SourceExpression(SourceSpan source_span, Node source_node)
        : span{source_span},
          node{std::move(source_node)} {}

    ~SourceExpression();
    SourceExpression(const SourceExpression&) = delete;
    auto operator=(const SourceExpression&) -> SourceExpression& = delete;
    SourceExpression(SourceExpression&&) noexcept = default;
    auto operator=(SourceExpression&&) noexcept -> SourceExpression& = default;

    SourceSpan span{};
    Node node{};
};

struct SourceWildcardProjection final {
    SourceSpan span{};
    SourceQualifiedName qualifier;
};

struct SourceProjectionItem final {
    using Node = std::variant<SourceExpressionPtr, SourceWildcardProjection>;

    SourceSpan span{};
    Node node{};
    std::optional<SourceIdentifier> alias;
};

struct SourceSortItem final {
    SourceSpan span{};
    SourceExpressionPtr expression;
    SortDirection direction{SortDirection::kDefault};
    NullOrder null_order{NullOrder::kDefault};
};

struct SourceNamedTable final {
    SourceQualifiedName name;
    std::optional<SourceIdentifier> alias;
};

struct SourceJoinOnCondition final {
    SourceExpressionPtr expression;
};

struct SourceJoinUsingCondition final {
    std::vector<SourceIdentifier> columns;
};

struct SourceJoinTable final {
    using Condition = std::variant<std::monostate, SourceJoinOnCondition, SourceJoinUsingCondition>;

    JoinKind kind{JoinKind::kInner};
    SourceTableReferencePtr left;
    SourceTableReferencePtr right;
    Condition condition{};
};

struct SourceDerivedTable final {
    bool lateral{false};
    SourceQueryPtr query;
    std::optional<SourceIdentifier> alias;
};

struct SourceTableReference final {
    using Node = std::variant<SourceNamedTable, SourceJoinTable, SourceDerivedTable>;

    SourceTableReference() = default;

    SourceTableReference(SourceSpan source_span, Node source_node)
        : span{source_span},
          node{std::move(source_node)} {}

    ~SourceTableReference();
    SourceTableReference(const SourceTableReference&) = delete;
    auto operator=(const SourceTableReference&) -> SourceTableReference& = delete;
    SourceTableReference(SourceTableReference&&) noexcept = default;
    auto operator=(SourceTableReference&&) noexcept -> SourceTableReference& = default;

    SourceSpan span{};
    Node node{};
};

struct SourceSelectQuery final {
    bool distinct{false};
    std::vector<SourceProjectionItem> projection;
    SourceTableReferencePtr from;
    SourceExpressionPtr filter;
    std::vector<SourceExpressionPtr> group_by;
    SourceExpressionPtr having;
    std::vector<SourceSortItem> order_by;
    SourceExpressionPtr limit;
    SourceExpressionPtr offset;
};

struct SourceValuesQuery final {
    std::vector<std::vector<SourceExpressionPtr>> rows;
};

struct SourceUnionQuery final {
    bool all{false};
    SourceQueryPtr left;
    SourceQueryPtr right;
    std::vector<SourceSortItem> order_by;
    SourceExpressionPtr limit;
    SourceExpressionPtr offset;
};

struct SourceQuery final {
    using Node = std::variant<SourceSelectQuery, SourceValuesQuery, SourceUnionQuery>;

    SourceQuery() = default;

    SourceQuery(SourceSpan source_span, Node source_node)
        : span{source_span},
          node{std::move(source_node)} {}

    ~SourceQuery();
    SourceQuery(const SourceQuery&) = delete;
    auto operator=(const SourceQuery&) -> SourceQuery& = delete;
    SourceQuery(SourceQuery&&) noexcept = default;
    auto operator=(SourceQuery&&) noexcept -> SourceQuery& = default;

    SourceSpan span{};
    Node node{};
};

struct SourceSelectStatement final {
    SourceQueryPtr query;
};

struct SourceInsertStatement final {
    SourceQualifiedName table;
    std::vector<SourceIdentifier> columns;
    SourceQueryPtr source;
};

struct SourceAssignment final {
    SourceIdentifier column;
    SourceExpressionPtr value;
};

struct SourceUpdateStatement final {
    SourceQualifiedName table;
    std::vector<SourceAssignment> assignments;
    SourceExpressionPtr filter;
};

struct SourceDeleteStatement final {
    SourceQualifiedName table;
    SourceExpressionPtr filter;
};

struct SourceColumnDefinition final {
    SourceIdentifier name;
    SourceTypeName data_type;
};

struct SourceCreateTableStatement final {
    SourceQualifiedName table;
    std::vector<SourceColumnDefinition> columns;
    // The source-side expression preserves spelling; binding validates it as
    // DB's single supported heap fillfactor option.
    SourceExpressionPtr fillfactor;
    bool if_not_exists{false};
};

struct SourceDropTableStatement final {
    std::vector<SourceQualifiedName> tables;
    bool if_exists{false};
};

struct SourceIndexElement final {
    SourceIdentifier column;
};

struct SourceCreateIndexStatement final {
    std::optional<SourceIdentifier> name;
    SourceQualifiedName table;
    std::vector<SourceIndexElement> elements;
    bool unique{false};
    bool if_not_exists{false};
    std::optional<SourceIdentifier> access_method;
};

struct SourceDropIndexStatement final {
    std::vector<SourceQualifiedName> indexes;
    bool if_exists{false};
};

struct SourceTransactionStatement final {
    TransactionKind kind{TransactionKind::kBegin};
};

struct SourceSetTransactionStatement final {
    IsolationLevel isolation{IsolationLevel::kSerializable};
};

struct SourceExplainStatement final {
    SourceStatementPtr statement;
    // Options stay source-level until the utility binder decides whether the
    // DB EXPLAIN surface accepts them.
    std::vector<SourceIdentifier> options;
};

struct SourceCopyOption final {
    SourceIdentifier name;
    SourceExpressionPtr value;
};

struct SourceCopyFromStatement final {
    SourceQualifiedName table;
    std::vector<SourceIdentifier> columns;
    std::vector<SourceCopyOption> options;
};

struct SourceShowTablesStatement final {};

struct SourceVacuumStatement final {
    std::optional<SourceQualifiedName> table;
};

struct SourceAnalyzeStatement final {
    std::optional<SourceQualifiedName> table;
};

struct SourceCheckpointStatement final {
    bool is_static{false};
};

struct SourceStatement final {
    using Node = std::variant<
        SourceSelectStatement,
        SourceInsertStatement,
        SourceUpdateStatement,
        SourceDeleteStatement,
        SourceCreateTableStatement,
        SourceDropTableStatement,
        SourceCreateIndexStatement,
        SourceDropIndexStatement,
        SourceTransactionStatement,
        SourceSetTransactionStatement,
        SourceExplainStatement,
        SourceCopyFromStatement,
        SourceShowTablesStatement,
        SourceVacuumStatement,
        SourceAnalyzeStatement,
        SourceCheckpointStatement>;

    SourceStatement() = default;

    SourceStatement(SourceSpan source_span, Node source_node)
        : span{source_span},
          node{std::move(source_node)} {}

    ~SourceStatement();
    SourceStatement(const SourceStatement&) = delete;
    auto operator=(const SourceStatement&) -> SourceStatement& = delete;
    SourceStatement(SourceStatement&&) noexcept = default;
    auto operator=(SourceStatement&&) noexcept -> SourceStatement& = default;

    SourceSpan span{};
    Node node{};
};

struct SourceParameterSlot final {
    ParameterSlot slot{kInvalidParameterSlot};
    std::vector<SourceSpan> occurrences;
};

// SourceScript owns the SQL bytes and every recursive source-AST node.
// It is deliberately move-only: copying an incomplete syntax tree would
// obscure ownership and accidentally make tree duplication a hot path.
struct SourceScript final {
    SourceScript() = default;

    explicit SourceScript(std::string source)
        : source_text{std::move(source)} {}

    SourceScript(const SourceScript&) = delete;
    auto operator=(const SourceScript&) -> SourceScript& = delete;
    SourceScript(SourceScript&&) noexcept = default;
    auto operator=(SourceScript&&) noexcept -> SourceScript& = default;
    ~SourceScript() = default;

    std::string source_text;
    std::vector<SourceStatement> statements;
    std::vector<SourceParameterSlot> parameter_slots;
};

inline SourceExpression::~SourceExpression() = default;
inline SourceTableReference::~SourceTableReference() = default;
inline SourceQuery::~SourceQuery() = default;
inline SourceStatement::~SourceStatement() = default;

inline void SourceQueryDeleter::operator()(SourceQuery* query) const noexcept {
    // std::unique_ptr transfers its owned object to this deleter protocol.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete query;
}

}  // namespace db::sql
