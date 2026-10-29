#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/planner/preprocess.hpp"
#include "db/sql/bound_ast.hpp"

#include "preprocess_internal.hpp"

namespace db::planner {
namespace {

template <typename... Visitors>
struct Overloaded final : Visitors... {
    using Visitors::operator()...;
};

template <typename... Visitors>
Overloaded(Visitors...) -> Overloaded<Visitors...>;

[[maybe_unused]] auto convert_cast_form([[maybe_unused]] const sql::BoundCastForm form) noexcept
    -> QueryCastForm {
    // LAB-S02.Q05:R60-BEGIN
    base::lab::missing_algorithm("S02.Q05:R60");
    // LAB-S02.Q05:R60-END
}

[[maybe_unused]] auto
convert_aggregate_kind([[maybe_unused]] const sql::BoundAggregateKind kind) noexcept
    -> QueryAggregateKind {
    // LAB-S02.Q05:R61-BEGIN
    base::lab::missing_algorithm("S02.Q05:R61");
    // LAB-S02.Q05:R61-END
}

auto convert_expression(sql::BoundExpressionPtr expression)
    -> base::Result<PreprocessedExpressionPtr>;
auto convert_query(sql::BoundQueryPtr query) -> base::Result<PreprocessedQueryPtr>;
auto convert_table_reference(sql::BoundTableReferencePtr table)
    -> base::Result<PreprocessedTableReferencePtr>;

[[maybe_unused]] auto convert_expression_list(
    [[maybe_unused]] std::vector<sql::BoundExpressionPtr> expressions,
    [[maybe_unused]] const std::string& context
) -> base::Result<std::vector<PreprocessedExpressionPtr>> {
    // LAB-S02.Q05:R62-BEGIN
    base::lab::missing_algorithm("S02.Q05:R62");
    // LAB-S02.Q05:R62-END
}

[[maybe_unused]] auto convert_expression_node([[maybe_unused]] sql::BoundLiteralExpression literal)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R63-BEGIN
    base::lab::missing_algorithm("S02.Q05:R63");
    // LAB-S02.Q05:R63-END
}

[[maybe_unused]] auto
convert_expression_node([[maybe_unused]] const sql::BoundColumnReference& column)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R64-BEGIN
    base::lab::missing_algorithm("S02.Q05:R64");
    // LAB-S02.Q05:R64-END
}

[[maybe_unused]] auto convert_expression_node([[maybe_unused]] const sql::BoundParameter& parameter)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R65-BEGIN
    base::lab::missing_algorithm("S02.Q05:R65");
    // LAB-S02.Q05:R65-END
}

[[maybe_unused]] auto convert_expression_node([[maybe_unused]] sql::BoundUnaryExpression unary)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R66-BEGIN
    base::lab::missing_algorithm("S02.Q05:R66");
    // LAB-S02.Q05:R66-END
}

[[maybe_unused]] auto convert_expression_node([[maybe_unused]] sql::BoundBinaryExpression binary)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R67-BEGIN
    base::lab::missing_algorithm("S02.Q05:R67");
    // LAB-S02.Q05:R67-END
}

[[maybe_unused]] auto convert_expression_node([[maybe_unused]] sql::BoundBooleanExpression boolean)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R68-BEGIN
    base::lab::missing_algorithm("S02.Q05:R68");
    // LAB-S02.Q05:R68-END
}

[[maybe_unused]] auto convert_expression_node([[maybe_unused]] sql::BoundIsNullExpression null_test)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R69-BEGIN
    base::lab::missing_algorithm("S02.Q05:R69");
    // LAB-S02.Q05:R69-END
}

[[maybe_unused]] auto convert_expression_node([[maybe_unused]] sql::BoundInListExpression in_list)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R70-BEGIN
    base::lab::missing_algorithm("S02.Q05:R70");
    // LAB-S02.Q05:R70-END
}

[[maybe_unused]] auto convert_expression_node([[maybe_unused]] sql::BoundBetweenExpression between)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R71-BEGIN
    base::lab::missing_algorithm("S02.Q05:R71");
    // LAB-S02.Q05:R71-END
}

[[maybe_unused]] auto convert_expression_node([[maybe_unused]] sql::BoundCastExpression cast)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R72-BEGIN
    base::lab::missing_algorithm("S02.Q05:R72");
    // LAB-S02.Q05:R72-END
}

[[maybe_unused]] auto
convert_expression_node([[maybe_unused]] sql::BoundAggregateExpression aggregate)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R73-BEGIN
    base::lab::missing_algorithm("S02.Q05:R73");
    // LAB-S02.Q05:R73-END
}

[[maybe_unused]] auto
convert_expression_node([[maybe_unused]] sql::BoundScalarSubqueryExpression scalar)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R74-BEGIN
    base::lab::missing_algorithm("S02.Q05:R74");
    // LAB-S02.Q05:R74-END
}

[[maybe_unused]] auto convert_expression_node([[maybe_unused]] sql::BoundExistsExpression exists)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R75-BEGIN
    base::lab::missing_algorithm("S02.Q05:R75");
    // LAB-S02.Q05:R75-END
}

[[maybe_unused]] auto
convert_expression_node([[maybe_unused]] sql::BoundInSubqueryExpression in_subquery)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R76-BEGIN
    base::lab::missing_algorithm("S02.Q05:R76");
    // LAB-S02.Q05:R76-END
}

[[maybe_unused]] auto convert_expression_variant([[maybe_unused]] sql::BoundExpression::Node node)
    -> base::Result<PreprocessedExpression::Node> {
    // LAB-S02.Q05:R77-BEGIN
    base::lab::missing_algorithm("S02.Q05:R77");
    // LAB-S02.Q05:R77-END
}

[[maybe_unused]] auto convert_expression([[maybe_unused]] sql::BoundExpressionPtr expression)
    -> base::Result<PreprocessedExpressionPtr> {
    // LAB-S02.Q05:R78-BEGIN
    base::lab::missing_algorithm("S02.Q05:R78");
    // LAB-S02.Q05:R78-END
}

[[maybe_unused]] auto
convert_join_condition([[maybe_unused]] sql::BoundJoinTable::Condition condition)
    -> base::Result<PreprocessedJoinTable::Condition> {
    // LAB-S02.Q05:R79-BEGIN
    base::lab::missing_algorithm("S02.Q05:R79");
    // LAB-S02.Q05:R79-END
}

[[maybe_unused]] auto convert_columns([[maybe_unused]] std::vector<sql::BoundColumn> columns)
    -> std::vector<QueryColumn> {
    // LAB-S02.Q05:R80-BEGIN
    base::lab::missing_algorithm("S02.Q05:R80");
    // LAB-S02.Q05:R80-END
}

[[maybe_unused]] auto convert_base_table([[maybe_unused]] sql::BoundBaseTable table)
    -> PreprocessedBaseTable {
    // LAB-S02.Q05:R81-BEGIN
    base::lab::missing_algorithm("S02.Q05:R81");
    // LAB-S02.Q05:R81-END
}

[[maybe_unused]] auto convert_table_node([[maybe_unused]] sql::BoundTableReference::Node node)
    -> base::Result<PreprocessedTableReference::Node> {
    // LAB-S02.Q05:R82-BEGIN
    base::lab::missing_algorithm("S02.Q05:R82");
    // LAB-S02.Q05:R82-END
}

[[maybe_unused]] auto convert_table_reference([[maybe_unused]] sql::BoundTableReferencePtr table)
    -> base::Result<PreprocessedTableReferencePtr> {
    // LAB-S02.Q05:R83-BEGIN
    base::lab::missing_algorithm("S02.Q05:R83");
    // LAB-S02.Q05:R83-END
}

[[maybe_unused]] auto convert_select([[maybe_unused]] sql::BoundSelectQuery select)
    -> base::Result<PreprocessedSelectQuery> {
    // LAB-S02.Q05:R84-BEGIN
    base::lab::missing_algorithm("S02.Q05:R84");
    // LAB-S02.Q05:R84-END
}

[[maybe_unused]] auto convert_query([[maybe_unused]] sql::BoundQueryPtr query)
    -> base::Result<PreprocessedQueryPtr> {
    // LAB-S02.Q05:R85-BEGIN
    base::lab::missing_algorithm("S02.Q05:R85");
    // LAB-S02.Q05:R85-END
}

[[maybe_unused]] auto convert_insert_values([[maybe_unused]] sql::BoundInsertValues values)
    -> base::Result<PreprocessedInsertValues> {
    // LAB-S02.Q05:R86-BEGIN
    base::lab::missing_algorithm("S02.Q05:R86");
    // LAB-S02.Q05:R86-END
}

[[maybe_unused]] auto convert_insert_source([[maybe_unused]] sql::BoundInsertSource source)
    -> base::Result<PreprocessedInsertSource> {
    // LAB-S02.Q05:R87-BEGIN
    base::lab::missing_algorithm("S02.Q05:R87");
    // LAB-S02.Q05:R87-END
}

[[maybe_unused]] auto convert_insert_statement([[maybe_unused]] sql::BoundInsertStatement statement)
    -> base::Result<PreprocessedStatement> {
    // LAB-S02.Q05:R88-BEGIN
    base::lab::missing_algorithm("S02.Q05:R88");
    // LAB-S02.Q05:R88-END
}

[[maybe_unused]] auto convert_update_statement([[maybe_unused]] sql::BoundUpdateStatement statement)
    -> base::Result<PreprocessedStatement> {
    // LAB-S02.Q05:R89-BEGIN
    base::lab::missing_algorithm("S02.Q05:R89");
    // LAB-S02.Q05:R89-END
}

[[maybe_unused]] auto convert_delete_statement([[maybe_unused]] sql::BoundDeleteStatement statement)
    -> base::Result<PreprocessedStatement> {
    // LAB-S02.Q05:R90-BEGIN
    base::lab::missing_algorithm("S02.Q05:R90");
    // LAB-S02.Q05:R90-END
}
}  // namespace

namespace detail {

auto preprocess_error(std::string message) -> base::Error {
    return {base::ErrorCode::kPlan, std::move(message)};
}

[[maybe_unused]] auto unsupported_query_shape([[maybe_unused]] std::string message) -> base::Error {
    // LAB-S02.Q05:R91-BEGIN
    base::lab::missing_algorithm("S02.Q05:R91");
    // LAB-S02.Q05:R91-END
}
}  // namespace detail

[[maybe_unused]] auto preprocess_bound_query([[maybe_unused]] sql::BoundQuery query)
    -> base::Result<PreprocessedQuery> {
    // LAB-S02.Q05:R92-BEGIN
    base::lab::missing_algorithm("S02.Q05:R92");
    // LAB-S02.Q05:R92-END
}

[[maybe_unused]] auto preprocess_bound_statement([[maybe_unused]] sql::BoundStatement statement)
    -> base::Result<PreprocessedStatement> {
    // LAB-S02.Q05:R93-BEGIN
    base::lab::missing_algorithm("S02.Q05:R93");
    // LAB-S02.Q05:R93-END
}

PreprocessedExpression::~PreprocessedExpression() = default;
PreprocessedTableReference::~PreprocessedTableReference() = default;
PreprocessedQuery::~PreprocessedQuery() = default;

}  // namespace db::planner
