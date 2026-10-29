#include "db/sql/binder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
#include "db/base/exception_boundary.hpp"
#include "db/base/lab_missing.hpp"
#include "db/types/coercion.hpp"
#include "db/types/operator_registry.hpp"
#include "db/types/type_io.hpp"
#include "db/types/type_registry.hpp"

#include "binder_scope_internal.hpp"

namespace db::sql {
namespace {

using detail::ambiguous_column;
using detail::bind_error;
using detail::bound_column_expression;
using detail::column_not_found;
using detail::duplicate_table_name;
using detail::last_name_part;
using detail::QueryScope;
using detail::ResolvedColumn;
using detail::unsupported_here;

inline constexpr base::ExceptionBoundaryPolicy kCatalogReadExceptionPolicy{
    .component = {},
    .allocation_message = "catalog read port exhausted available memory",
    .exception_prefix = "catalog read port threw an exception: ",
    .unknown_exception_message = "catalog read port threw a non-standard exception",
};

[[maybe_unused]] auto
lookup_catalog_table(const CatalogReadPort& catalog, const std::string_view name)
    -> base::Result<std::optional<CatalogTable>> {
    return base::invoke_exception_boundary(kCatalogReadExceptionPolicy, [&catalog, name] {
        return catalog.lookup_table(name);
    });
}

[[maybe_unused]] auto table_not_found(const std::string_view name) -> base::Error {
    std::string message{"table not found: "};
    message.append(name);
    return bind_error(std::move(message));
}

[[maybe_unused]] auto duplicate_column(const std::string_view name) -> base::Error {
    std::string message{"duplicate column: "};
    message.append(name);
    return bind_error(std::move(message));
}

[[maybe_unused]] auto
insert_column_count_mismatch(const std::size_t expected, const std::size_t actual) -> base::Error {
    std::string message{"INSERT column count mismatch: expected "};
    message.append(std::to_string(expected));
    message.append(", got ");
    message.append(std::to_string(actual));
    return {base::ErrorCode::kUnsupported, std::move(message)};
}

[[maybe_unused]] auto
parameter_not_declared(const ParameterSlot slot, const std::size_t declared_count) -> base::Error {
    std::string message{"parameter $"};
    message.append(std::to_string(slot.one_based()));
    message.append(" has no declared type (");
    message.append(std::to_string(declared_count));
    message.append(" declared)");
    return {base::ErrorCode::kUnsupported, std::move(message)};
}

[[maybe_unused]] auto invalid_parameter_type(const ParameterSlot slot) -> base::Error {
    std::string message{"parameter $"};
    message.append(std::to_string(slot.one_based()));
    message.append(" has an invalid or unregistered type");
    return {base::ErrorCode::kUnsupported, std::move(message)};
}

[[maybe_unused]] auto declared_parameter_not_used() -> base::Error {
    return {
        base::ErrorCode::kUnsupported,
        "not every declared parameter appears in SQL",
    };
}

[[maybe_unused]] auto cannot_coerce(
    const types::DataType source,
    const types::DataType target,
    const std::string_view context
) -> base::Error {
    std::string message{"cannot coerce type "};
    message.append(types::type_name(source.id()).value_or("<unknown>"));
    message.append(" to ");
    message.append(types::type_name(target.id()).value_or("<unknown>"));
    message.append(" in ");
    message.append(context);
    message.append(" context");
    return bind_error(std::move(message));
}

[[maybe_unused]] auto invalid_literal(const types::DataType target, const std::string_view detail)
    -> base::Error {
    std::string message{"invalid literal for "};
    message.append(types::type_name(target.id()).value_or("<unknown>"));
    message.append(": ");
    message.append(detail);
    return bind_error(std::move(message));
}

[[maybe_unused]] auto operator_not_defined(
    const BinaryOperator operation,
    const types::DataType left,
    const types::DataType right
) -> base::Error {
    std::string message{"operator is not defined for "};
    message.append(types::type_name(left.id()).value_or("<unknown>"));
    message.append(" and ");
    message.append(types::type_name(right.id()).value_or("<unknown>"));
    message.append(" (operator #");
    message.append(std::to_string(static_cast<std::uint32_t>(operation)));
    message.push_back(')');
    return bind_error(std::move(message));
}

[[maybe_unused]] auto type_operator([[maybe_unused]] const BinaryOperator operation) noexcept
    -> std::optional<types::BinaryOperator> {
    static_cast<void>(operation);
    // LAB-S02.Q02:R15-BEGIN
    base::lab::missing_algorithm("S02.Q02:R15");
    // LAB-S02.Q02:R15-END
}

[[maybe_unused]] auto type_operator([[maybe_unused]] const BooleanOperator operation) noexcept
    -> std::optional<types::BooleanOperator> {
    static_cast<void>(operation);
    // LAB-S02.Q02:R16-BEGIN
    base::lab::missing_algorithm("S02.Q02:R16");
    // LAB-S02.Q02:R16-END
}

[[maybe_unused]] auto is_numeric([[maybe_unused]] const types::DataType data_type) noexcept
    -> bool {
    static_cast<void>(data_type);
    // LAB-S02.Q02:R17-BEGIN
    base::lab::missing_algorithm("S02.Q02:R17");
    // LAB-S02.Q02:R17-END
}

[[maybe_unused]] auto is_unknown([[maybe_unused]] const types::DataType data_type) noexcept
    -> bool {
    static_cast<void>(data_type);
    // LAB-S02.Q02:R18-BEGIN
    base::lab::missing_algorithm("S02.Q02:R18");
    // LAB-S02.Q02:R18-END
}

[[maybe_unused]] auto unmodified_type([[maybe_unused]] const types::DataType data_type) noexcept
    -> types::DataType {
    static_cast<void>(data_type);
    // LAB-S02.Q02:R19-BEGIN
    base::lab::missing_algorithm("S02.Q02:R19");
    // LAB-S02.Q02:R19-END
}

[[maybe_unused]] auto ascii_case_equal(
    [[maybe_unused]] const std::string_view left,
    [[maybe_unused]] const std::string_view right
) noexcept -> bool {
    static_cast<void>(left);
    static_cast<void>(right);
    // LAB-S02.Q03:R31-BEGIN
    base::lab::missing_algorithm("S02.Q03:R31");
    // LAB-S02.Q03:R31-END
}

[[maybe_unused]] auto source_type([[maybe_unused]] const SourceTypeName& source)
    -> base::Result<types::DataType> {
    static_cast<void>(source);
    // LAB-S02.Q04:R01-BEGIN
    base::lab::missing_algorithm("S02.Q04:R01");
    // LAB-S02.Q04:R01-END
}

[[maybe_unused]] auto owned_last_name_part(
    [[maybe_unused]] const SourceQualifiedName& name,
    [[maybe_unused]] const std::string_view subject
) -> base::Result<std::string> {
    static_cast<void>(name);
    static_cast<void>(subject);
    // LAB-S02.Q03:R32-BEGIN
    base::lab::missing_algorithm("S02.Q03:R32");
    // LAB-S02.Q03:R32-END
}

auto bind_query(
    const SourceQuery& source,
    const CatalogReadPort& catalog,
    std::span<const types::DataType> parameter_types,
    const QueryScope* outer
) -> base::Result<BoundQueryPtr>;

auto bind_select_query(
    const SourceQuery& source,
    const CatalogReadPort& catalog,
    std::span<const types::DataType> parameter_types,
    const QueryScope* outer,
    bool resolve_unknown = true
) -> base::Result<BoundSelectQuery>;

auto bind_statement(
    const SourceStatement& source,
    const CatalogReadPort& catalog,
    std::span<const types::DataType> parameter_types
) -> base::Result<BoundStatement>;

auto bind_explain_statement(
    SourceSpan span,
    const SourceExplainStatement& source,
    const CatalogReadPort& catalog,
    std::span<const types::DataType> parameter_types
) -> base::Result<BoundStatement>;

auto derived_columns(const BoundSelectQuery& query) -> base::Result<std::vector<BoundColumn>>;

// Bound trees own a schema-shaped copy rather than retaining a catalog DTO.
// Keeping the conversion in one place prevents SELECT and DML lookup paths
// from gradually acquiring different ownership or range-slot behavior.
[[maybe_unused]] auto materialize_bound_base_table(
    [[maybe_unused]] CatalogTable descriptor,
    [[maybe_unused]] std::string name,
    [[maybe_unused]] std::optional<std::string> alias,
    [[maybe_unused]] const BoundRangeTableIndex range_table_index
) -> BoundBaseTable {
    static_cast<void>(descriptor);
    static_cast<void>(name);
    static_cast<void>(alias);
    static_cast<void>(range_table_index);
    // LAB-S06.Q05:R08-BEGIN
    base::lab::missing_algorithm("S06.Q05:R08");
    // LAB-S06.Q05:R08-END
}

[[maybe_unused]] auto lookup_dml_target_table(
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const SourceQualifiedName& name
) -> base::Result<BoundBaseTable> {
    static_cast<void>(catalog);
    static_cast<void>(name);
    // LAB-S02.Q03:R23-BEGIN
    base::lab::missing_algorithm("S02.Q03:R23");
    // LAB-S02.Q03:R23-END
}

[[maybe_unused]] auto resolve_target_column_index(
    [[maybe_unused]] const std::vector<BoundColumn>& columns,
    [[maybe_unused]] const std::string_view name
) -> base::Result<BoundColumnIndex> {
    static_cast<void>(columns);
    static_cast<void>(name);
    // LAB-S02.Q03:R24-BEGIN
    base::lab::missing_algorithm("S02.Q03:R24");
    // LAB-S02.Q03:R24-END
}

[[maybe_unused]] auto bind_named_table(
    [[maybe_unused]] QueryScope& scope,
    [[maybe_unused]] const SourceNamedTable& source
) -> base::Result<BoundBaseTable> {
    static_cast<void>(scope);
    static_cast<void>(source);
    // LAB-S02.Q03:R22-BEGIN
    base::lab::missing_algorithm("S02.Q03:R22");
    // LAB-S02.Q03:R22-END
}

[[maybe_unused]] auto bind_derived_table(
    [[maybe_unused]] QueryScope& scope,
    [[maybe_unused]] const SourceDerivedTable& source
) -> base::Result<BoundDerivedTable> {
    static_cast<void>(scope);
    static_cast<void>(source);
    // LAB-S06.Q05:R06-BEGIN
    base::lab::missing_algorithm("S06.Q05:R06");
    // LAB-S06.Q05:R06-END
}

[[maybe_unused]] auto
bound_expression(const SourceSpan span, const types::DataType data_type, BoundExpression::Node node)
    -> BoundExpressionPtr {
    return std::make_unique<BoundExpression>(span, data_type, std::move(node));
}

[[maybe_unused]] auto bind_literal(
    [[maybe_unused]] const SourceLiteralExpression& source,
    [[maybe_unused]] const SourceSpan span
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(span);
    // LAB-S02.Q04:R02-BEGIN
    base::lab::missing_algorithm("S02.Q04:R02");
    // LAB-S02.Q04:R02-END
}

[[maybe_unused]] auto coerce_expression(
    [[maybe_unused]] BoundExpressionPtr expression,
    [[maybe_unused]] const types::DataType target,
    [[maybe_unused]] const types::CoercionContext context,
    [[maybe_unused]] const BoundCastForm form
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(expression);
    static_cast<void>(target);
    static_cast<void>(context);
    static_cast<void>(form);
    // LAB-S02.Q04:R03-BEGIN
    base::lab::missing_algorithm("S02.Q04:R03");
    // LAB-S02.Q04:R03-END
}

[[maybe_unused]] auto
common_data_type([[maybe_unused]] const std::vector<BoundExpressionPtr>& expressions)
    -> base::Result<types::DataType> {
    static_cast<void>(expressions);
    // LAB-S02.Q04:R04-BEGIN
    base::lab::missing_algorithm("S02.Q04:R04");
    // LAB-S02.Q04:R04-END
}

[[maybe_unused]] auto
coerce_to_common_type([[maybe_unused]] std::vector<BoundExpressionPtr> expressions)
    -> base::Result<std::vector<BoundExpressionPtr>> {
    static_cast<void>(expressions);
    // LAB-S02.Q04:R05-BEGIN
    base::lab::missing_algorithm("S02.Q04:R05");
    // LAB-S02.Q04:R05-END
}

[[maybe_unused]] auto coerce_comparison_pair(
    [[maybe_unused]] BoundExpressionPtr left,
    [[maybe_unused]] BoundExpressionPtr right
) -> base::Result<std::pair<BoundExpressionPtr, BoundExpressionPtr>> {
    static_cast<void>(left);
    static_cast<void>(right);
    // LAB-S02.Q04:R06-BEGIN
    base::lab::missing_algorithm("S02.Q04:R06");
    // LAB-S02.Q04:R06-END
}

[[maybe_unused]] auto coerce_numeric_pair(
    [[maybe_unused]] BoundExpressionPtr left,
    [[maybe_unused]] BoundExpressionPtr right
) -> base::Result<std::pair<BoundExpressionPtr, BoundExpressionPtr>> {
    static_cast<void>(left);
    static_cast<void>(right);
    // LAB-S02.Q04:R07-BEGIN
    base::lab::missing_algorithm("S02.Q04:R07");
    // LAB-S02.Q04:R07-END
}

auto bind_expression(const SourceExpression& source, const QueryScope& scope)
    -> base::Result<BoundExpressionPtr>;

auto bind_binary_expression(
    SourceSpan span,
    BinaryOperator operation,
    BoundExpressionPtr left,
    BoundExpressionPtr right
) -> base::Result<BoundExpressionPtr>;

[[maybe_unused]] auto bind_unary_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R08-BEGIN
    base::lab::missing_algorithm("S02.Q04:R08");
    // LAB-S02.Q04:R08-END
}

[[maybe_unused]] auto bind_source_binary_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R09-BEGIN
    base::lab::missing_algorithm("S02.Q04:R09");
    // LAB-S02.Q04:R09-END
}

[[maybe_unused]] auto bind_null_test_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R10-BEGIN
    base::lab::missing_algorithm("S02.Q04:R10");
    // LAB-S02.Q04:R10-END
}

[[maybe_unused]] auto bind_expression_list(
    [[maybe_unused]] const std::vector<SourceExpressionPtr>& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<std::vector<BoundExpressionPtr>> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R11-BEGIN
    base::lab::missing_algorithm("S02.Q04:R11");
    // LAB-S02.Q04:R11-END
}

// Aggregates in a nested subquery belong to that query level, not to the
// expression being bound in this query.  This mirrors the boundary later
// planning uses when it assigns aggregate slots to a particular grouping
// operation.
[[maybe_unused]] auto
bound_expression_has_aggregate([[maybe_unused]] const BoundExpression& expression) noexcept
    -> bool {
    static_cast<void>(expression);
    // LAB-S05.Q04:R01-BEGIN
    base::lab::missing_algorithm("S05.Q04:R01");
    // LAB-S05.Q04:R01-END
}

[[maybe_unused]] auto aggregate_kind([[maybe_unused]] const std::string_view name) noexcept
    -> std::optional<BoundAggregateKind> {
    static_cast<void>(name);
    // LAB-S05.Q04:R02-BEGIN
    base::lab::missing_algorithm("S05.Q04:R02");
    // LAB-S05.Q04:R02-END
}

[[maybe_unused]] auto aggregate_name([[maybe_unused]] const BoundAggregateKind kind) noexcept
    -> std::string_view {
    static_cast<void>(kind);
    // LAB-S05.Q04:R10-BEGIN
    base::lab::missing_algorithm("S05.Q04:R10");
    // LAB-S05.Q04:R10-END
}

[[maybe_unused]] auto bind_aggregate_arguments(
    [[maybe_unused]] const SourceFunctionCallExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<std::vector<BoundExpressionPtr>> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S05.Q04:R03-BEGIN
    base::lab::missing_algorithm("S05.Q04:R03");
    // LAB-S05.Q04:R03-END
}

[[maybe_unused]] auto
aggregate_unknown_target([[maybe_unused]] const BoundAggregateKind kind) noexcept
    -> types::DataType {
    static_cast<void>(kind);
    // LAB-S05.Q04:R04-BEGIN
    base::lab::missing_algorithm("S05.Q04:R04");
    // LAB-S05.Q04:R04-END
}

[[maybe_unused]] auto bind_aggregate_star(
    [[maybe_unused]] const SourceFunctionCallExpression& source,
    [[maybe_unused]] const BoundAggregateKind kind,
    [[maybe_unused]] const std::vector<BoundExpressionPtr>& arguments
) -> base::Result<void> {
    static_cast<void>(source);
    static_cast<void>(kind);
    static_cast<void>(arguments);
    // LAB-S05.Q04:R05-BEGIN
    base::lab::missing_algorithm("S05.Q04:R05");
    // LAB-S05.Q04:R05-END
}

[[maybe_unused]] auto bind_aggregate_argument(
    [[maybe_unused]] const SourceFunctionCallExpression& source,
    [[maybe_unused]] const BoundAggregateKind kind,
    [[maybe_unused]] std::vector<BoundExpressionPtr>& arguments
) -> base::Result<void> {
    static_cast<void>(source);
    static_cast<void>(kind);
    static_cast<void>(arguments);
    // LAB-S05.Q04:R06-BEGIN
    base::lab::missing_algorithm("S05.Q04:R06");
    // LAB-S05.Q04:R06-END
}

[[maybe_unused]] auto aggregate_result_type(
    [[maybe_unused]] const BoundAggregateKind kind,
    [[maybe_unused]] const std::vector<BoundExpressionPtr>& arguments
) -> types::DataType {
    static_cast<void>(kind);
    static_cast<void>(arguments);
    // LAB-S05.Q04:R07-BEGIN
    base::lab::missing_algorithm("S05.Q04:R07");
    // LAB-S05.Q04:R07-END
}

[[maybe_unused]] auto bind_function_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S05.Q04:R08-BEGIN
    base::lab::missing_algorithm("S05.Q04:R08");
    // LAB-S05.Q04:R08-END
}

[[maybe_unused]] auto bind_in_list_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R12-BEGIN
    base::lab::missing_algorithm("S02.Q04:R12");
    // LAB-S02.Q04:R12-END
}

[[maybe_unused]] auto bind_between_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R13-BEGIN
    base::lab::missing_algorithm("S02.Q04:R13");
    // LAB-S02.Q04:R13-END
}

[[maybe_unused]] auto bind_cast_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R14-BEGIN
    base::lab::missing_algorithm("S02.Q04:R14");
    // LAB-S02.Q04:R14-END
}

[[maybe_unused]] auto ensure_single_subquery_column(
    [[maybe_unused]] const BoundQuery& query,
    [[maybe_unused]] const std::string_view subject
) -> base::Status {
    static_cast<void>(query);
    static_cast<void>(subject);
    // LAB-S07.Q08:R03-BEGIN
    base::lab::missing_algorithm("S07.Q08:R03");
    // LAB-S07.Q08:R03-END
}

[[maybe_unused]] auto bind_scalar_subquery_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S07.Q08:R04-BEGIN
    base::lab::missing_algorithm("S07.Q08:R04");
    // LAB-S07.Q08:R04-END
}

[[maybe_unused]] auto bind_exists_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S07.Q08:R05-BEGIN
    base::lab::missing_algorithm("S07.Q08:R05");
    // LAB-S07.Q08:R05-END
}

[[maybe_unused]] auto bind_in_subquery_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S07.Q08:R06-BEGIN
    base::lab::missing_algorithm("S07.Q08:R06");
    // LAB-S07.Q08:R06-END
}

[[maybe_unused]] auto bind_binary_expression(
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const BinaryOperator operation,
    [[maybe_unused]] BoundExpressionPtr left,
    [[maybe_unused]] BoundExpressionPtr right
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(span);
    static_cast<void>(operation);
    static_cast<void>(left);
    static_cast<void>(right);
    // LAB-S02.Q04:R15-BEGIN
    base::lab::missing_algorithm("S02.Q04:R15");
    // LAB-S02.Q04:R15-END
}

[[maybe_unused]] auto bind_boolean_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R16-BEGIN
    base::lab::missing_algorithm("S02.Q04:R16");
    // LAB-S02.Q04:R16-END
}

[[maybe_unused]] auto bind_expression(
    [[maybe_unused]] const SourceExpression& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R17-BEGIN
    base::lab::missing_algorithm("S02.Q04:R17");
    // LAB-S02.Q04:R17-END
}

[[maybe_unused]] auto bound_column_projection(
    const SourceSpan span,
    const ResolvedColumn& column,
    std::optional<std::string> alias = std::nullopt
) -> BoundProjectionItem {
    BoundProjectionItem item{};
    item.expression = bound_column_expression(span, column);
    item.alias = std::move(alias);
    return item;
}

using BoundRangeTableColumns = std::vector<std::span<const BoundColumn>>;

// FROM binding assigns dense query-local range-table indexes and completed joins own both
// children. Build the direct-address view once so a wide derived projection does not rescan the
// join tree for every unaliased source column.
[[maybe_unused]] void collect_bound_range_table_columns(
    [[maybe_unused]] const BoundTableReference& table,
    [[maybe_unused]] BoundRangeTableColumns& columns
) {
    static_cast<void>(table);
    static_cast<void>(columns);
    // LAB-S02.Q03:R33-BEGIN
    base::lab::missing_algorithm("S02.Q03:R33");
    // LAB-S02.Q03:R33-END
}

// A derived table publishes independent columns. Preserve an explicit SELECT alias; otherwise only
// a direct resolved column reference has a stable source-column name to expose.
[[maybe_unused]] auto derived_columns([[maybe_unused]] const BoundSelectQuery& query)
    -> base::Result<std::vector<BoundColumn>> {
    static_cast<void>(query);
    // LAB-S06.Q05:R07-BEGIN
    base::lab::missing_algorithm("S06.Q05:R07");
    // LAB-S06.Q05:R07-END
}

struct BoundJoinInputs final {
    BoundTableReferencePtr left;
    BoundTableReferencePtr right;
};

auto bind_from(const SourceTableReferencePtr& source, QueryScope& scope)
    -> base::Result<BoundTableReferencePtr>;

[[maybe_unused]] auto
bind_join_inputs([[maybe_unused]] const SourceJoinTable& source, [[maybe_unused]] QueryScope& scope)
    -> base::Result<BoundJoinInputs> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S07.Q01:R04-BEGIN
    base::lab::missing_algorithm("S07.Q01:R04");
    // LAB-S07.Q01:R04-END
}

[[maybe_unused]] auto bind_join_on_condition(
    [[maybe_unused]] const SourceJoinOnCondition& source,
    [[maybe_unused]] const QueryScope& scope,
    [[maybe_unused]] const BoundTableReference& left,
    [[maybe_unused]] const BoundTableReference& right
) -> base::Result<BoundJoinOnCondition> {
    static_cast<void>(source);
    static_cast<void>(scope);
    static_cast<void>(left);
    static_cast<void>(right);
    // LAB-S07.Q01:R05-BEGIN
    base::lab::missing_algorithm("S07.Q01:R05");
    // LAB-S07.Q01:R05-END
}

[[maybe_unused]] auto bind_join_using_condition(
    [[maybe_unused]] const SourceJoinUsingCondition& source,
    [[maybe_unused]] const QueryScope& scope,
    [[maybe_unused]] const BoundTableReference& left,
    [[maybe_unused]] const BoundTableReference& right
) -> base::Result<BoundJoinUsingCondition> {
    static_cast<void>(source);
    static_cast<void>(scope);
    static_cast<void>(left);
    static_cast<void>(right);
    // LAB-S07.Q01:R06-BEGIN
    base::lab::missing_algorithm("S07.Q01:R06");
    // LAB-S07.Q01:R06-END
}

[[maybe_unused]] auto make_bound_join(
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const JoinKind kind,
    [[maybe_unused]] BoundJoinInputs inputs,
    [[maybe_unused]] BoundJoinTable::Condition condition
) -> BoundTableReferencePtr {
    static_cast<void>(span);
    static_cast<void>(kind);
    static_cast<void>(inputs);
    static_cast<void>(condition);
    // LAB-S07.Q01:R07-BEGIN
    base::lab::missing_algorithm("S07.Q01:R07");
    // LAB-S07.Q01:R07-END
}

[[maybe_unused]] auto bind_join(
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const SourceJoinTable& source,
    [[maybe_unused]] QueryScope& scope
) -> base::Result<BoundTableReferencePtr> {
    static_cast<void>(span);
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S07.Q01:R08-BEGIN
    base::lab::missing_algorithm("S07.Q01:R08");
    // LAB-S07.Q01:R08-END
}

[[maybe_unused]] auto bind_from(
    [[maybe_unused]] const SourceTableReferencePtr& source,
    [[maybe_unused]] QueryScope& scope
) -> base::Result<BoundTableReferencePtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q03:R27-BEGIN
    base::lab::missing_algorithm("S02.Q03:R27");
    // LAB-S02.Q03:R27-END
}

[[maybe_unused]] auto bind_projection_item(
    [[maybe_unused]] const SourceProjectionItem& source,
    [[maybe_unused]] QueryScope& scope
) -> base::Result<BoundProjectionItem> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q03:R25-BEGIN
    base::lab::missing_algorithm("S02.Q03:R25");
    // LAB-S02.Q03:R25-END
}

[[maybe_unused]] auto bind_projection(
    [[maybe_unused]] const SourceSelectQuery& source,
    [[maybe_unused]] QueryScope& scope
) -> base::Result<std::vector<BoundProjectionItem>> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q03:R26-BEGIN
    base::lab::missing_algorithm("S02.Q03:R26");
    // LAB-S02.Q03:R26-END
}

[[maybe_unused]] auto resolve_projection_output_types(
    [[maybe_unused]] std::vector<BoundProjectionItem>& projection,
    [[maybe_unused]] QueryScope& scope
) -> base::Status {
    static_cast<void>(projection);
    static_cast<void>(scope);
    // LAB-S02.Q04:R20-BEGIN
    base::lab::missing_algorithm("S02.Q04:R20");
    // LAB-S02.Q04:R20-END
}

[[maybe_unused]] auto bind_order_by(
    [[maybe_unused]] const std::vector<SourceSortItem>& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<std::vector<BoundSortItem>> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R21-BEGIN
    base::lab::missing_algorithm("S02.Q04:R21");
    // LAB-S02.Q04:R21-END
}

[[maybe_unused]] auto bind_group_by(
    [[maybe_unused]] const std::vector<SourceExpressionPtr>& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<std::vector<BoundExpressionPtr>> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S05.Q04:R09-BEGIN
    base::lab::missing_algorithm("S05.Q04:R09");
    // LAB-S05.Q04:R09-END
}

// HAVING and DML filters share the same optional boolean-expression contract.
// Keep the nullable handling and coercion in one place so these clauses cannot
// drift to different binding semantics.
[[maybe_unused]] auto bind_optional_boolean_expression(
    [[maybe_unused]] const SourceExpressionPtr& source,
    [[maybe_unused]] const QueryScope& scope
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    // LAB-S02.Q04:R22-BEGIN
    base::lab::missing_algorithm("S02.Q04:R22");
    // LAB-S02.Q04:R22-END
}

[[maybe_unused]] auto bind_row_count(
    [[maybe_unused]] const SourceExpressionPtr& source,
    [[maybe_unused]] const QueryScope& scope,
    [[maybe_unused]] const std::string_view clause
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(source);
    static_cast<void>(scope);
    static_cast<void>(clause);
    // LAB-S02.Q04:R18-BEGIN
    base::lab::missing_algorithm("S02.Q04:R18");
    // LAB-S02.Q04:R18-END
}

[[maybe_unused]] auto bind_union_query(
    [[maybe_unused]] const SourceUnionQuery& source,
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types,
    [[maybe_unused]] const QueryScope* outer
) -> base::Result<BoundSelectQuery> {
    static_cast<void>(source);
    static_cast<void>(span);
    static_cast<void>(catalog);
    static_cast<void>(parameter_types);
    static_cast<void>(outer);
    // LAB-S06.Q01:R01-BEGIN
    base::lab::missing_algorithm("S06.Q01:R01");
    // LAB-S06.Q01:R01-END
}

[[maybe_unused]] auto bind_select_query(
    [[maybe_unused]] const SourceQuery& source,
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types,
    [[maybe_unused]] const QueryScope* outer,
    [[maybe_unused]] const bool resolve_unknown
) -> base::Result<BoundSelectQuery> {
    static_cast<void>(source);
    static_cast<void>(catalog);
    static_cast<void>(parameter_types);
    static_cast<void>(outer);
    static_cast<void>(resolve_unknown);
    // LAB-S02.Q04:R23-BEGIN
    base::lab::missing_algorithm("S02.Q04:R23");
    // LAB-S02.Q04:R23-END
}

[[maybe_unused]] auto bind_query(
    const SourceQuery& source,
    const CatalogReadPort& catalog,
    const std::span<const types::DataType> parameter_types,
    const QueryScope* outer
) -> base::Result<BoundQueryPtr> {
    auto select = bind_select_query(source, catalog, parameter_types, outer);
    if (!select) {
        return base::fail(std::move(select).error());
    }
    return BoundQueryPtr{std::make_unique<BoundQuery>(source.span, std::move(select).value())};
}

inline constexpr std::uint8_t kSqlDefaultHeapFillfactor{80U};
inline constexpr std::uint8_t kSqlMinimumHeapFillfactor{10U};

[[maybe_unused]] auto dml_scope(
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const BoundBaseTable& target,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<QueryScope> {
    static_cast<void>(catalog);
    static_cast<void>(target);
    static_cast<void>(parameter_types);
    // LAB-S02.Q03:R34-BEGIN
    base::lab::missing_algorithm("S02.Q03:R34");
    // LAB-S02.Q03:R34-END
}

[[maybe_unused]] auto resolve_insert_target_columns(
    [[maybe_unused]] const std::vector<SourceIdentifier>& source_columns,
    [[maybe_unused]] const std::vector<BoundColumn>& table_columns
) -> base::Result<std::vector<BoundColumnIndex>> {
    static_cast<void>(source_columns);
    static_cast<void>(table_columns);
    // LAB-S02.Q03:R35-BEGIN
    base::lab::missing_algorithm("S02.Q03:R35");
    // LAB-S02.Q03:R35-END
}

[[maybe_unused]] auto copy_option_string(
    [[maybe_unused]] const SourceCopyOption& option,
    [[maybe_unused]] const std::string_view option_name
) -> base::Result<std::string> {
    static_cast<void>(option);
    static_cast<void>(option_name);
    // LAB-S02.Q04:R24-BEGIN
    base::lab::missing_algorithm("S02.Q04:R24");
    // LAB-S02.Q04:R24-END
}

[[maybe_unused]] auto reject_duplicate_copy_option(
    [[maybe_unused]] bool& seen,
    [[maybe_unused]] const std::string_view option_name
) -> base::Status {
    static_cast<void>(seen);
    static_cast<void>(option_name);
    // LAB-S02.Q04:R25-BEGIN
    base::lab::missing_algorithm("S02.Q04:R25");
    // LAB-S02.Q04:R25-END
}

[[maybe_unused]] auto bind_copy_format_option(
    [[maybe_unused]] const SourceCopyOption& option,
    [[maybe_unused]] bool& seen_format
) -> base::Status {
    static_cast<void>(option);
    static_cast<void>(seen_format);
    // LAB-S02.Q04:R26-BEGIN
    base::lab::missing_algorithm("S02.Q04:R26");
    // LAB-S02.Q04:R26-END
}

[[maybe_unused]] auto bind_copy_delimiter_option(
    [[maybe_unused]] const SourceCopyOption& option,
    [[maybe_unused]] BoundCopyFromStatement& bound,
    [[maybe_unused]] bool& seen_delimiter
) -> base::Status {
    static_cast<void>(option);
    static_cast<void>(bound);
    static_cast<void>(seen_delimiter);
    // LAB-S02.Q04:R27-BEGIN
    base::lab::missing_algorithm("S02.Q04:R27");
    // LAB-S02.Q04:R27-END
}

[[maybe_unused]] auto bind_copy_null_option(
    [[maybe_unused]] const SourceCopyOption& option,
    [[maybe_unused]] BoundCopyFromStatement& bound,
    [[maybe_unused]] bool& seen_null
) -> base::Status {
    static_cast<void>(option);
    static_cast<void>(bound);
    static_cast<void>(seen_null);
    // LAB-S02.Q04:R28-BEGIN
    base::lab::missing_algorithm("S02.Q04:R28");
    // LAB-S02.Q04:R28-END
}

[[maybe_unused]] auto bind_copy_statement(
    [[maybe_unused]] const SourceCopyFromStatement& source,
    [[maybe_unused]] const CatalogReadPort& catalog
) -> base::Result<BoundCopyFromStatement> {
    static_cast<void>(source);
    static_cast<void>(catalog);
    // LAB-S02.Q04:R29-BEGIN
    base::lab::missing_algorithm("S02.Q04:R29");
    // LAB-S02.Q04:R29-END
}

[[maybe_unused]] auto coerce_assignment(
    [[maybe_unused]] BoundExpressionPtr expression,
    [[maybe_unused]] const BoundColumn& column
) -> base::Result<BoundExpressionPtr> {
    static_cast<void>(expression);
    static_cast<void>(column);
    // LAB-S02.Q04:R19-BEGIN
    base::lab::missing_algorithm("S02.Q04:R19");
    // LAB-S02.Q04:R19-END
}

[[maybe_unused]] auto bind_insert_values(
    [[maybe_unused]] const SourceValuesQuery& source,
    [[maybe_unused]] const QueryScope& scope,
    [[maybe_unused]] const std::vector<BoundColumn>& table_columns,
    [[maybe_unused]] const std::vector<BoundColumnIndex>& target_columns
) -> base::Result<BoundInsertValues> {
    static_cast<void>(source);
    static_cast<void>(scope);
    static_cast<void>(table_columns);
    static_cast<void>(target_columns);
    // LAB-S02.Q04:R30-BEGIN
    base::lab::missing_algorithm("S02.Q04:R30");
    // LAB-S02.Q04:R30-END
}

[[maybe_unused]] auto bind_insert_query(
    [[maybe_unused]] const SourceQuery& source,
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const std::vector<BoundColumn>& table_columns,
    [[maybe_unused]] const std::vector<BoundColumnIndex>& target_columns,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<BoundInsertQuery> {
    static_cast<void>(source);
    static_cast<void>(catalog);
    static_cast<void>(table_columns);
    static_cast<void>(target_columns);
    static_cast<void>(parameter_types);
    // LAB-S02.Q04:R31-BEGIN
    base::lab::missing_algorithm("S02.Q04:R31");
    // LAB-S02.Q04:R31-END
}

[[maybe_unused]] auto bind_insert_statement(
    [[maybe_unused]] const SourceInsertStatement& source,
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<BoundInsertStatement> {
    static_cast<void>(source);
    static_cast<void>(catalog);
    static_cast<void>(parameter_types);
    // LAB-S02.Q03:R28-BEGIN
    base::lab::missing_algorithm("S02.Q03:R28");
    // LAB-S02.Q03:R28-END
}

[[maybe_unused]] auto bind_update_statement(
    [[maybe_unused]] const SourceUpdateStatement& source,
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<BoundUpdateStatement> {
    static_cast<void>(source);
    static_cast<void>(catalog);
    static_cast<void>(parameter_types);
    // LAB-S02.Q03:R29-BEGIN
    base::lab::missing_algorithm("S02.Q03:R29");
    // LAB-S02.Q03:R29-END
}

[[maybe_unused]] auto bind_delete_statement(
    [[maybe_unused]] const SourceDeleteStatement& source,
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<BoundDeleteStatement> {
    static_cast<void>(source);
    static_cast<void>(catalog);
    static_cast<void>(parameter_types);
    // LAB-S02.Q03:R30-BEGIN
    base::lab::missing_algorithm("S02.Q03:R30");
    // LAB-S02.Q03:R30-END
}

[[maybe_unused]] auto bind_heap_fillfactor([[maybe_unused]] const SourceExpressionPtr& source)
    -> base::Result<std::uint8_t> {
    static_cast<void>(source);
    // LAB-S02.Q04:R32-BEGIN
    base::lab::missing_algorithm("S02.Q04:R32");
    // LAB-S02.Q04:R32-END
}

[[maybe_unused]] auto
bind_create_table_statement([[maybe_unused]] const SourceCreateTableStatement& source)
    -> base::Result<BoundCreateTableStatement> {
    static_cast<void>(source);
    // LAB-S02.Q04:R33-BEGIN
    base::lab::missing_algorithm("S02.Q04:R33");
    // LAB-S02.Q04:R33-END
}

[[maybe_unused]] auto
bind_drop_table_statement([[maybe_unused]] const SourceDropTableStatement& source)
    -> base::Result<BoundDropTableStatement> {
    static_cast<void>(source);
    // LAB-S02.Q03:R37-BEGIN
    base::lab::missing_algorithm("S02.Q03:R37");
    // LAB-S02.Q03:R37-END
}

[[maybe_unused]] auto automatic_index_name(
    [[maybe_unused]] const BoundBaseTable& table,
    [[maybe_unused]] const std::vector<SourceIndexElement>& elements
) -> std::string {
    static_cast<void>(table);
    static_cast<void>(elements);
    // LAB-S02.Q03:R36-BEGIN
    base::lab::missing_algorithm("S02.Q03:R36");
    // LAB-S02.Q03:R36-END
}

[[maybe_unused]] auto bind_create_index_statement(
    [[maybe_unused]] const SourceCreateIndexStatement& source,
    [[maybe_unused]] const CatalogReadPort& catalog
) -> base::Result<BoundCreateIndexStatement> {
    // LAB-S02.Q04:R2001-BEGIN
    base::lab::missing_algorithm("S02.Q04:R2001");
    // LAB-S02.Q04:R2001-END
}

[[maybe_unused]] auto
bind_drop_index_statement([[maybe_unused]] const SourceDropIndexStatement& source)
    -> base::Result<BoundDropIndexStatement> {
    static_cast<void>(source);
    // LAB-S02.Q03:R38-BEGIN
    base::lab::missing_algorithm("S02.Q03:R38");
    // LAB-S02.Q03:R38-END
}

[[maybe_unused]] auto bind_maintenance_table(
    [[maybe_unused]] const std::optional<SourceQualifiedName>& source,
    [[maybe_unused]] const CatalogReadPort& catalog
) -> base::Result<std::optional<base::Oid>> {
    static_cast<void>(source);
    static_cast<void>(catalog);
    // LAB-S02.Q03:R39-BEGIN
    base::lab::missing_algorithm("S02.Q03:R39");
    // LAB-S02.Q03:R39-END
}

[[maybe_unused]] auto bind_select_statement(
    const SourceSpan span,
    const SourceSelectStatement& source,
    const CatalogReadPort& catalog,
    const std::span<const types::DataType> parameter_types
) -> base::Result<BoundStatement> {
    if (source.query == nullptr) {
        return base::fail(bind_error("SELECT statement has no query"));
    }
    auto bound = bind_select_query(*source.query, catalog, parameter_types, nullptr);
    if (!bound) {
        return base::fail(std::move(bound).error());
    }
    return BoundStatement{span, std::move(bound).value()};
}

template <typename Source, typename Bound, typename Binder>
[[maybe_unused]] auto
bind_wrapped_statement(const SourceSpan span, const Source& source, Binder&& binder)
    -> base::Result<BoundStatement> {
    auto bound = std::forward<Binder>(binder)(source);
    if (!bound) {
        return base::fail(std::move(bound).error());
    }
    return BoundStatement{span, Bound{std::move(bound).value()}};
}

[[maybe_unused]] auto bind_explain_statement(
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const SourceExplainStatement& source,
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<BoundStatement> {
    static_cast<void>(span);
    static_cast<void>(source);
    static_cast<void>(catalog);
    static_cast<void>(parameter_types);
    // LAB-S02.Q03:R40-BEGIN
    base::lab::missing_algorithm("S02.Q03:R40");
    // LAB-S02.Q03:R40-END
}

[[maybe_unused]] auto bind_vacuum_statement(
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const SourceVacuumStatement& source,
    [[maybe_unused]] const CatalogReadPort& catalog
) -> base::Result<BoundStatement> {
    static_cast<void>(span);
    static_cast<void>(source);
    static_cast<void>(catalog);
    // LAB-S02.Q03:R41-BEGIN
    base::lab::missing_algorithm("S02.Q03:R41");
    // LAB-S02.Q03:R41-END
}

[[maybe_unused]] auto bind_analyze_statement(
    [[maybe_unused]] const SourceSpan span,
    [[maybe_unused]] const SourceAnalyzeStatement& source,
    [[maybe_unused]] const CatalogReadPort& catalog
) -> base::Result<BoundStatement> {
    static_cast<void>(span);
    static_cast<void>(source);
    static_cast<void>(catalog);
    // LAB-S02.Q03:R42-BEGIN
    base::lab::missing_algorithm("S02.Q03:R42");
    // LAB-S02.Q03:R42-END
}

[[maybe_unused]] auto bind_statement(
    [[maybe_unused]] const SourceStatement& source,
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<BoundStatement> {
    // LAB-S02.Q04:R2002-BEGIN
    base::lab::missing_algorithm("S02.Q04:R2002");
    // LAB-S02.Q04:R2002-END
}

[[maybe_unused]] auto collect_parameter_slots(
    [[maybe_unused]] const SourceScript& source,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<std::vector<const SourceParameterSlot*>> {
    static_cast<void>(source);
    static_cast<void>(parameter_types);
    // LAB-S02.Q04:R34-BEGIN
    base::lab::missing_algorithm("S02.Q04:R34");
    // LAB-S02.Q04:R34-END
}

[[maybe_unused]] auto validate_parameter_slots(
    [[maybe_unused]] const std::vector<const SourceParameterSlot*>& source_slots,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<void> {
    static_cast<void>(source_slots);
    static_cast<void>(parameter_types);
    // LAB-S02.Q04:R35-BEGIN
    base::lab::missing_algorithm("S02.Q04:R35");
    // LAB-S02.Q04:R35-END
}

[[maybe_unused]] auto copy_parameter_slots(
    const std::vector<const SourceParameterSlot*>& source_slots,
    const std::span<const types::DataType> parameter_types
) -> std::vector<BoundParameterSlot> {
    std::vector<BoundParameterSlot> bound_slots;
    bound_slots.reserve(source_slots.size());
    for (std::size_t index = 0U; index < source_slots.size(); ++index) {
        const SourceParameterSlot& source_slot = *source_slots.at(index);
        bound_slots.push_back(
            BoundParameterSlot{
                .slot = source_slot.slot,
                .data_type = parameter_types[index],
                .occurrences = source_slot.occurrences,
            }
        );
    }
    return bound_slots;
}

[[maybe_unused]] auto bind_script_statements(
    [[maybe_unused]] const SourceScript& source,
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<std::vector<BoundStatement>> {
    static_cast<void>(source);
    static_cast<void>(catalog);
    static_cast<void>(parameter_types);
    // LAB-S02.Q04:R36-BEGIN
    base::lab::missing_algorithm("S02.Q04:R36");
    // LAB-S02.Q04:R36-END
}

}  // namespace

auto bind_script(const CatalogReadPort& catalog, const SourceScript& source)
    -> base::Result<BoundScript> {
    return bind_script_with_parameters(catalog, source, {});
}

[[maybe_unused]] auto bind_script_with_parameters(
    [[maybe_unused]] const CatalogReadPort& catalog,
    [[maybe_unused]] const SourceScript& source,
    [[maybe_unused]] const std::span<const types::DataType> parameter_types
) -> base::Result<BoundScript> {
    static_cast<void>(catalog);
    static_cast<void>(source);
    static_cast<void>(parameter_types);
    // LAB-S02.Q04:R37-BEGIN
    base::lab::missing_algorithm("S02.Q04:R37");
    // LAB-S02.Q04:R37-END
}

}  // namespace db::sql
