#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/sql/bound_ast.hpp"
#include "db/sql/catalog_port.hpp"
#include "db/sql/source_ast.hpp"
#include "db/types/data_type.hpp"

namespace db::sql::detail {

struct ResolvedColumn;

auto bind_error(std::string message) -> base::Error;
auto unsupported_here(std::string_view shape) -> base::Error;
auto column_not_found(std::string_view name) -> base::Error;
auto ambiguous_column(std::string_view name) -> base::Error;
auto duplicate_table_name(std::string_view name) -> base::Error;
auto last_name_part(const SourceQualifiedName& name, std::string_view subject)
    -> base::Result<std::string_view>;
auto bound_column_expression(SourceSpan span, const ResolvedColumn& column) -> BoundExpressionPtr;

struct ResolvedColumn final {
    std::string_view name;
    BoundColumnReference reference;
};

// A query scope borrows table schemas and projection expressions only while
// one query level is being bound. It is the DB-C++ equivalent of the
// narrow name-resolution part of PostgreSQL's ParseState, without catalog or
// memory-context ownership escaping the bind call.
struct ProjectionAliasBinding final {
    std::string name;
    const BoundExpression* expression{nullptr};
};

struct ScopeEntry final {
    std::string_view qualifier;
    std::span<const BoundColumn> columns;
    BoundRangeTableIndex range_table_index{0U};

    ScopeEntry(
        std::string_view qualifier_value,
        std::span<const BoundColumn> columns_value,
        BoundRangeTableIndex range_table_index_value
    );

    mutable std::uint32_t linear_lookups{0U};
    mutable std::unique_ptr<std::vector<std::size_t>> column_index;
};

enum class ColumnLookupState : std::uint8_t {
    kMissing,
    kResolved,
    kAmbiguous,
};

struct ColumnLookup final {
    ColumnLookupState state{ColumnLookupState::kMissing};
    std::optional<ResolvedColumn> column;
};

class QueryScope final {
public:
    explicit QueryScope(
        const CatalogReadPort& catalog,
        std::span<const types::DataType> parameter_types = {},
        const QueryScope* outer = nullptr
    );

    QueryScope(const QueryScope&) = delete;
    auto operator=(const QueryScope&) -> QueryScope& = delete;
    QueryScope(QueryScope&&) noexcept = default;
    auto operator=(QueryScope&&) noexcept -> QueryScope& = default;
    ~QueryScope() = default;

    auto catalog() const noexcept -> const CatalogReadPort&;
    auto parameter_types() const noexcept -> std::span<const types::DataType>;
    auto outer() const noexcept -> const QueryScope*;
    auto contains_qualifier(std::string_view qualifier) const noexcept -> bool;
    auto next_range_table_index() const -> base::Result<BoundRangeTableIndex>;

    auto add_dml_target(const BoundBaseTable& table) -> base::Status;
    auto add_bound_table_reference(const BoundTableReference& table) -> base::Status;

    static auto
    for_table_reference(const QueryScope& source_scope, const BoundTableReference& table)
        -> base::Result<QueryScope>;
    static auto for_join_inputs(
        const QueryScope& source_scope,
        const BoundTableReference& left,
        const BoundTableReference& right
    ) -> base::Result<QueryScope>;

    auto bind_column_reference(SourceSpan span, const SourceQualifiedName& name) const
        -> base::Result<BoundExpressionPtr>;
    auto lookup_from_column(const SourceQualifiedName& name) const -> base::Result<ColumnLookup>;

    auto declare_projection_alias(std::string_view name, const BoundExpression& expression)
        -> base::Status;
    auto refresh_projection_alias(std::string_view name, const BoundExpression& expression)
        -> base::Status;
    auto expand_wildcard(const SourceQualifiedName& qualifier) const
        -> base::Result<std::vector<ResolvedColumn>>;

private:
    auto bind_unqualified_column_reference(SourceSpan span, const SourceQualifiedName& name) const
        -> base::Result<BoundExpressionPtr>;
    auto bind_qualified_column_reference(SourceSpan span, const SourceQualifiedName& name) const
        -> base::Result<BoundExpressionPtr>;
    auto bind_outer_column_reference(SourceSpan span, const SourceQualifiedName& name) const
        -> base::Result<BoundExpressionPtr>;

    static auto missing_column_reference(const SourceQualifiedName& name)
        -> base::Result<BoundExpressionPtr>;
    static auto append_scope_entry(ScopeEntry entry, std::vector<ScopeEntry>& entries)
        -> base::Status;
    static auto
    append_bound_table_entries(const BoundTableReference& table, std::vector<ScopeEntry>& entries)
        -> base::Status;

    auto entry_for(std::string_view qualifier) const noexcept -> const ScopeEntry*;
    static auto resolve_in_entry(const ScopeEntry& entry, std::string_view name)
        -> base::Result<std::optional<ResolvedColumn>>;
    static auto resolve_in_entry_linear(const ScopeEntry& entry, std::string_view name)
        -> base::Result<std::optional<ResolvedColumn>>;
    static auto build_column_index(const ScopeEntry& entry) -> bool;
    static auto resolve_in_index(const ScopeEntry& entry, std::string_view name)
        -> base::Result<std::optional<ResolvedColumn>>;

    auto bind_projection_alias(SourceSpan span, std::string_view name) const
        -> base::Result<std::optional<BoundExpressionPtr>>;
    auto lookup_unqualified_from(std::string_view name) const -> base::Result<ColumnLookup>;
    auto lookup_projection_alias(std::string_view name) const
        -> base::Result<std::optional<const BoundExpression*>>;
    auto lookup_qualified(std::string_view qualifier, std::string_view name) const
        -> base::Result<ColumnLookup>;

    static auto checked_index(std::size_t index, std::string_view subject)
        -> base::Result<std::uint32_t>;
    static auto append_all_columns(const ScopeEntry& entry, std::vector<ResolvedColumn>& output)
        -> base::Status;

    const CatalogReadPort& catalog_;
    std::span<const types::DataType> parameter_types_;
    const QueryScope* outer_{nullptr};
    std::vector<ScopeEntry> entries_;
    std::vector<ProjectionAliasBinding> aliases_;
};

}  // namespace db::sql::detail
