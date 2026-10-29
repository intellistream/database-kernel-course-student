#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/checked.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "binder_scope_internal.hpp"
#include "bound_clone_internal.hpp"

namespace db::sql::detail {

auto bind_error(std::string message) -> base::Error {
    return {base::ErrorCode::kBind, std::move(message)};
}

auto unsupported_here(const std::string_view shape) -> base::Error {
    std::string message{"SQL "};
    message.append(shape);
    message.append(" is outside the supported DB-C++ SQL surface");
    return {base::ErrorCode::kUnsupported, std::move(message)};
}

auto column_not_found(const std::string_view name) -> base::Error {
    std::string message{"column not found: "};
    message.append(name);
    return bind_error(std::move(message));
}

auto ambiguous_column(const std::string_view name) -> base::Error {
    std::string message{"column reference is ambiguous: "};
    message.append(name);
    return bind_error(std::move(message));
}

auto duplicate_table_name(const std::string_view name) -> base::Error {
    std::string message{"table name appears more than once: "};
    message.append(name);
    return bind_error(std::move(message));
}

auto last_name_part(const SourceQualifiedName& name, const std::string_view subject)
    -> base::Result<std::string_view> {
    if (!name.parts.empty() && !name.parts.back().spelling.empty()) {
        return std::string_view{name.parts.back().spelling};
    }
    std::string message{"empty "};
    message.append(subject);
    return base::fail(bind_error(std::move(message)));
}

auto bound_column_expression(const SourceSpan span, const ResolvedColumn& column)
    -> BoundExpressionPtr {
    auto expression = std::make_unique<BoundExpression>();
    expression->span = span;
    expression->data_type = column.reference.data_type;
    expression->node = column.reference;
    return expression;
}

ScopeEntry::ScopeEntry(
    const std::string_view qualifier_value,
    const std::span<const BoundColumn> columns_value,
    const BoundRangeTableIndex range_table_index_value
)
    : qualifier{qualifier_value},
      columns{columns_value},
      range_table_index{range_table_index_value} {}

QueryScope::QueryScope(
    const CatalogReadPort& catalog,
    const std::span<const types::DataType> parameter_types,
    const QueryScope* const outer
)
    : catalog_{catalog},
      parameter_types_{parameter_types},
      outer_{outer} {}

auto QueryScope::catalog() const noexcept -> const CatalogReadPort& {
    return catalog_;
}

auto QueryScope::parameter_types() const noexcept -> std::span<const types::DataType> {
    return parameter_types_;
}

auto QueryScope::outer() const noexcept -> const QueryScope* {
    return outer_;
}

auto QueryScope::contains_qualifier(const std::string_view qualifier) const noexcept -> bool {
    return entry_for(qualifier) != nullptr;
}

auto QueryScope::next_range_table_index() const -> base::Result<BoundRangeTableIndex> {
    auto index = checked_index(entries_.size(), "range-table entry count");
    if (!index) {
        return base::fail(std::move(index).error());
    }
    return BoundRangeTableIndex{index.value()};
}

[[maybe_unused]] auto QueryScope::add_dml_target([[maybe_unused]] const BoundBaseTable& table)
    -> base::Status {
    static_cast<void>(table);
    // LAB-S02.Q03:R01-BEGIN
    base::lab::missing_algorithm("S02.Q03:R01");
    // LAB-S02.Q03:R01-END
}

[[maybe_unused]] auto
QueryScope::add_bound_table_reference([[maybe_unused]] const BoundTableReference& table)
    -> base::Status {
    static_cast<void>(table);
    // LAB-S02.Q03:R02-BEGIN
    base::lab::missing_algorithm("S02.Q03:R02");
    // LAB-S02.Q03:R02-END
}

[[maybe_unused]] auto QueryScope::for_table_reference(
    [[maybe_unused]] const QueryScope& source_scope,
    [[maybe_unused]] const BoundTableReference& table
) -> base::Result<QueryScope> {
    static_cast<void>(source_scope);
    static_cast<void>(table);
    // LAB-S02.Q03:R03-BEGIN
    base::lab::missing_algorithm("S02.Q03:R03");
    // LAB-S02.Q03:R03-END
}

[[maybe_unused]] auto QueryScope::for_join_inputs(
    [[maybe_unused]] const QueryScope& source_scope,
    [[maybe_unused]] const BoundTableReference& left,
    [[maybe_unused]] const BoundTableReference& right
) -> base::Result<QueryScope> {
    static_cast<void>(source_scope);
    static_cast<void>(left);
    static_cast<void>(right);
    // LAB-S07.Q01:R03-BEGIN
    base::lab::missing_algorithm("S07.Q01:R03");
    // LAB-S07.Q01:R03-END
}

[[maybe_unused]] auto QueryScope::bind_column_reference(
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const SourceQualifiedName& name
) const -> base::Result<BoundExpressionPtr> {
    static_cast<void>(span);
    static_cast<void>(name);
    // LAB-S02.Q03:R04-BEGIN
    base::lab::missing_algorithm("S02.Q03:R04");
    // LAB-S02.Q03:R04-END
}

[[maybe_unused]] auto
QueryScope::lookup_from_column([[maybe_unused]] const SourceQualifiedName& name) const
    -> base::Result<ColumnLookup> {
    static_cast<void>(name);
    // LAB-S02.Q03:R05-BEGIN
    base::lab::missing_algorithm("S02.Q03:R05");
    // LAB-S02.Q03:R05-END
}

[[maybe_unused]] auto QueryScope::declare_projection_alias(
    [[maybe_unused]] const std::string_view name,
    [[maybe_unused]] const BoundExpression& expression
) -> base::Status {
    static_cast<void>(name);
    static_cast<void>(expression);
    // LAB-S02.Q03:R06-BEGIN
    base::lab::missing_algorithm("S02.Q03:R06");
    // LAB-S02.Q03:R06-END
}

[[maybe_unused]] auto QueryScope::refresh_projection_alias(
    [[maybe_unused]] const std::string_view name,
    [[maybe_unused]] const BoundExpression& expression
) -> base::Status {
    static_cast<void>(name);
    static_cast<void>(expression);
    // LAB-S02.Q03:R07-BEGIN
    base::lab::missing_algorithm("S02.Q03:R07");
    // LAB-S02.Q03:R07-END
}

[[maybe_unused]] auto
QueryScope::expand_wildcard([[maybe_unused]] const SourceQualifiedName& qualifier) const
    -> base::Result<std::vector<ResolvedColumn>> {
    static_cast<void>(qualifier);
    // LAB-S02.Q03:R08-BEGIN
    base::lab::missing_algorithm("S02.Q03:R08");
    // LAB-S02.Q03:R08-END
}

[[maybe_unused]] auto QueryScope::bind_unqualified_column_reference(
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const SourceQualifiedName& name
) const -> base::Result<BoundExpressionPtr> {
    static_cast<void>(span);
    static_cast<void>(name);
    // LAB-S02.Q03:R09-BEGIN
    base::lab::missing_algorithm("S02.Q03:R09");
    // LAB-S02.Q03:R09-END
}

[[maybe_unused]] auto QueryScope::bind_qualified_column_reference(
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const SourceQualifiedName& name
) const -> base::Result<BoundExpressionPtr> {
    static_cast<void>(span);
    static_cast<void>(name);
    // LAB-S02.Q03:R10-BEGIN
    base::lab::missing_algorithm("S02.Q03:R10");
    // LAB-S02.Q03:R10-END
}

[[maybe_unused]] auto QueryScope::bind_outer_column_reference(
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const SourceQualifiedName& name
) const -> base::Result<BoundExpressionPtr> {
    static_cast<void>(span);
    static_cast<void>(name);
    // LAB-S07.Q08:R02-BEGIN
    base::lab::missing_algorithm("S07.Q08:R02");
    // LAB-S07.Q08:R02-END
}

auto QueryScope::missing_column_reference(const SourceQualifiedName& name)
    -> base::Result<BoundExpressionPtr> {
    if (name.parts.size() == 2U) {
        std::string qualified{name.parts.front().spelling};
        qualified.push_back('.');
        qualified.append(name.parts.back().spelling);
        return base::fail(column_not_found(qualified));
    }
    auto part = last_name_part(name, "column reference");
    if (!part) {
        return base::fail(std::move(part).error());
    }
    return base::fail(column_not_found(part.value()));
}

[[maybe_unused]] auto QueryScope::append_scope_entry(
    [[maybe_unused]] ScopeEntry entry,
    [[maybe_unused]] std::vector<ScopeEntry>& entries
) -> base::Status {
    static_cast<void>(entry);
    static_cast<void>(entries);
    // LAB-S02.Q03:R11-BEGIN
    base::lab::missing_algorithm("S02.Q03:R11");
    // LAB-S02.Q03:R11-END
}

[[maybe_unused]] auto QueryScope::append_bound_table_entries(
    [[maybe_unused]] const BoundTableReference& table,
    [[maybe_unused]] std::vector<ScopeEntry>& entries
) -> base::Status {
    static_cast<void>(table);
    static_cast<void>(entries);
    // LAB-S02.Q03:R12-BEGIN
    base::lab::missing_algorithm("S02.Q03:R12");
    // LAB-S02.Q03:R12-END
}

auto QueryScope::entry_for(const std::string_view qualifier) const noexcept -> const ScopeEntry* {
    for (const ScopeEntry& entry : entries_) {
        if (entry.qualifier == qualifier) {
            return std::addressof(entry);
        }
    }
    return nullptr;
}

[[maybe_unused]] auto QueryScope::resolve_in_entry(
    [[maybe_unused]] const ScopeEntry& entry,
    [[maybe_unused]] const std::string_view name
) -> base::Result<std::optional<ResolvedColumn>> {
    static_cast<void>(entry);
    static_cast<void>(name);
    // LAB-S02.Q03:R13-BEGIN
    base::lab::missing_algorithm("S02.Q03:R13");
    // LAB-S02.Q03:R13-END
}

[[maybe_unused]] auto QueryScope::resolve_in_entry_linear(
    [[maybe_unused]] const ScopeEntry& entry,
    [[maybe_unused]] const std::string_view name
) -> base::Result<std::optional<ResolvedColumn>> {
    static_cast<void>(entry);
    static_cast<void>(name);
    // LAB-S02.Q03:R14-BEGIN
    base::lab::missing_algorithm("S02.Q03:R14");
    // LAB-S02.Q03:R14-END
}

[[maybe_unused]] auto QueryScope::build_column_index([[maybe_unused]] const ScopeEntry& entry)
    -> bool {
    static_cast<void>(entry);
    // LAB-S02.Q03:R15-BEGIN
    base::lab::missing_algorithm("S02.Q03:R15");
    // LAB-S02.Q03:R15-END
}

[[maybe_unused]] auto QueryScope::resolve_in_index(
    [[maybe_unused]] const ScopeEntry& entry,
    [[maybe_unused]] const std::string_view name
) -> base::Result<std::optional<ResolvedColumn>> {
    static_cast<void>(entry);
    static_cast<void>(name);
    // LAB-S02.Q03:R16-BEGIN
    base::lab::missing_algorithm("S02.Q03:R16");
    // LAB-S02.Q03:R16-END
}

[[maybe_unused]] auto QueryScope::bind_projection_alias(
    [[maybe_unused]] const SourceSpan /*span*/,
    [[maybe_unused]] const std::string_view name
) const -> base::Result<std::optional<BoundExpressionPtr>> {
    static_cast<void>(name);
    // LAB-S02.Q03:R17-BEGIN
    base::lab::missing_algorithm("S02.Q03:R17");
    // LAB-S02.Q03:R17-END
}

[[maybe_unused]] auto
QueryScope::lookup_unqualified_from([[maybe_unused]] const std::string_view name) const
    -> base::Result<ColumnLookup> {
    static_cast<void>(name);
    // LAB-S02.Q03:R18-BEGIN
    base::lab::missing_algorithm("S02.Q03:R18");
    // LAB-S02.Q03:R18-END
}

[[maybe_unused]] auto
QueryScope::lookup_projection_alias([[maybe_unused]] const std::string_view name) const
    -> base::Result<std::optional<const BoundExpression*>> {
    static_cast<void>(name);
    // LAB-S02.Q03:R19-BEGIN
    base::lab::missing_algorithm("S02.Q03:R19");
    // LAB-S02.Q03:R19-END
}

[[maybe_unused]] auto QueryScope::lookup_qualified(
    [[maybe_unused]] const std::string_view qualifier,
    [[maybe_unused]] const std::string_view name
) const -> base::Result<ColumnLookup> {
    static_cast<void>(qualifier);
    static_cast<void>(name);
    // LAB-S02.Q03:R20-BEGIN
    base::lab::missing_algorithm("S02.Q03:R20");
    // LAB-S02.Q03:R20-END
}

auto QueryScope::checked_index(const std::size_t index, const std::string_view subject)
    -> base::Result<std::uint32_t> {
    auto narrowed = base::checked_cast<std::uint32_t>(index);
    if (narrowed) {
        return narrowed;
    }
    std::string message{"SQL "};
    message.append(subject);
    message.append(" exceeds binder limit");
    return base::fail(base::Error{base::ErrorCode::kResourceExhausted, std::move(message)});
}

[[maybe_unused]] auto QueryScope::append_all_columns(
    [[maybe_unused]] const ScopeEntry& entry,
    [[maybe_unused]] std::vector<ResolvedColumn>& output
) -> base::Status {
    static_cast<void>(entry);
    static_cast<void>(output);
    // LAB-S02.Q03:R21-BEGIN
    base::lab::missing_algorithm("S02.Q03:R21");
    // LAB-S02.Q03:R21-END
}

}  // namespace db::sql::detail
