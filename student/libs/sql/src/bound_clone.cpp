#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"

#include "bound_clone_internal.hpp"

namespace db::sql::detail {
namespace {

auto clone_error(std::string message) -> base::Error {
    return {base::ErrorCode::kBind, std::move(message)};
}

auto clone_unsupported(const std::string_view shape) -> base::Error {
    std::string message{"SQL "};
    message.append(shape);
    message.append(" is outside the supported DB-C++ SQL surface");
    return {base::ErrorCode::kUnsupported, std::move(message)};
}

auto make_bound_expression(
    const SourceSpan span,
    const types::DataType data_type,
    BoundExpression::Node node
) -> BoundExpressionPtr {
    return std::make_unique<BoundExpression>(span, data_type, std::move(node));
}

auto clone_expression(const BoundExpression& source) -> base::Result<BoundExpressionPtr>;

auto clone_expression_list(const std::vector<BoundExpressionPtr>& source)
    -> base::Result<std::vector<BoundExpressionPtr>> {
    std::vector<BoundExpressionPtr> cloned;
    cloned.reserve(source.size());
    for (const BoundExpressionPtr& expression : source) {
        if (expression == nullptr) {
            return base::fail(clone_error("bound expression list has an absent expression"));
        }
        auto copied = clone_expression(*expression);
        if (!copied) {
            return base::fail(std::move(copied).error());
        }
        cloned.push_back(std::move(copied).value());
    }
    return cloned;
}

auto clone_bound_unary_expression(const BoundExpression& source, const BoundUnaryExpression& node)
    -> base::Result<BoundExpressionPtr> {
    if (node.operand == nullptr) {
        return base::fail(clone_error("bound unary expression has no operand"));
    }
    auto operand = clone_expression(*node.operand);
    if (!operand) {
        return base::fail(std::move(operand).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundUnaryExpression{
            .op = node.op,
            .operand = std::move(operand).value(),
        }
    );
}

auto clone_bound_binary_expression(const BoundExpression& source, const BoundBinaryExpression& node)
    -> base::Result<BoundExpressionPtr> {
    if (node.left == nullptr || node.right == nullptr) {
        return base::fail(clone_error("bound binary expression has an absent operand"));
    }
    auto left = clone_expression(*node.left);
    if (!left) {
        return base::fail(std::move(left).error());
    }
    auto right = clone_expression(*node.right);
    if (!right) {
        return base::fail(std::move(right).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundBinaryExpression{
            .op = node.op,
            .left = std::move(left).value(),
            .right = std::move(right).value(),
        }
    );
}

auto clone_bound_boolean_expression(
    const BoundExpression& source,
    const BoundBooleanExpression& node
) -> base::Result<BoundExpressionPtr> {
    auto operands = clone_expression_list(node.operands);
    if (!operands) {
        return base::fail(std::move(operands).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundBooleanExpression{
            .op = node.op,
            .operands = std::move(operands).value(),
        }
    );
}

auto clone_bound_null_test_expression(
    const BoundExpression& source,
    const BoundIsNullExpression& node
) -> base::Result<BoundExpressionPtr> {
    if (node.operand == nullptr) {
        return base::fail(clone_error("bound NULL test has no operand"));
    }
    auto operand = clone_expression(*node.operand);
    if (!operand) {
        return base::fail(std::move(operand).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundIsNullExpression{
            .negated = node.negated,
            .operand = std::move(operand).value(),
        }
    );
}

auto clone_bound_in_list_expression(
    const BoundExpression& source,
    const BoundInListExpression& node
) -> base::Result<BoundExpressionPtr> {
    if (node.operand == nullptr) {
        return base::fail(clone_error("bound IN list has no operand"));
    }
    auto operand = clone_expression(*node.operand);
    if (!operand) {
        return base::fail(std::move(operand).error());
    }
    auto values = clone_expression_list(node.values);
    if (!values) {
        return base::fail(std::move(values).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundInListExpression{
            .negated = node.negated,
            .operand = std::move(operand).value(),
            .values = std::move(values).value(),
        }
    );
}

auto clone_bound_between_expression(
    const BoundExpression& source,
    const BoundBetweenExpression& node
) -> base::Result<BoundExpressionPtr> {
    if (node.operand == nullptr || node.lower == nullptr || node.upper == nullptr) {
        return base::fail(clone_error("bound BETWEEN expression has an absent operand"));
    }
    auto operand = clone_expression(*node.operand);
    if (!operand) {
        return base::fail(std::move(operand).error());
    }
    auto lower = clone_expression(*node.lower);
    if (!lower) {
        return base::fail(std::move(lower).error());
    }
    auto upper = clone_expression(*node.upper);
    if (!upper) {
        return base::fail(std::move(upper).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundBetweenExpression{
            .negated = node.negated,
            .operand = std::move(operand).value(),
            .lower = std::move(lower).value(),
            .upper = std::move(upper).value(),
        }
    );
}

auto clone_bound_cast_expression(const BoundExpression& source, const BoundCastExpression& node)
    -> base::Result<BoundExpressionPtr> {
    if (node.operand == nullptr) {
        return base::fail(clone_error("bound cast expression has no operand"));
    }
    auto operand = clone_expression(*node.operand);
    if (!operand) {
        return base::fail(std::move(operand).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundCastExpression{
            .operand = std::move(operand).value(),
            .target_type = node.target_type,
            .path = node.path,
            .form = node.form,
        }
    );
}

auto clone_bound_aggregate_expression(
    const BoundExpression& source,
    const BoundAggregateExpression& node
) -> base::Result<BoundExpressionPtr> {
    auto arguments = clone_expression_list(node.arguments);
    if (!arguments) {
        return base::fail(std::move(arguments).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundAggregateExpression{
            .kind = node.kind,
            .distinct = node.distinct,
            .star = node.star,
            .arguments = std::move(arguments).value(),
            .aggregate_index = node.aggregate_index,
        }
    );
}

auto clone_bound_query(const BoundQuery& source) -> base::Result<BoundQueryPtr>;

auto clone_bound_scalar_subquery_expression(
    const BoundExpression& source,
    const BoundScalarSubqueryExpression& node
) -> base::Result<BoundExpressionPtr> {
    if (node.query == nullptr) {
        return base::fail(clone_error("bound scalar subquery has no query"));
    }
    auto query = clone_bound_query(*node.query);
    if (!query) {
        return base::fail(std::move(query).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundScalarSubqueryExpression{.query = std::move(query).value()}
    );
}

auto clone_bound_exists_expression(const BoundExpression& source, const BoundExistsExpression& node)
    -> base::Result<BoundExpressionPtr> {
    if (node.query == nullptr) {
        return base::fail(clone_error("bound EXISTS expression has no query"));
    }
    auto query = clone_bound_query(*node.query);
    if (!query) {
        return base::fail(std::move(query).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundExistsExpression{
            .negated = node.negated,
            .query = std::move(query).value(),
        }
    );
}

auto clone_bound_in_subquery_expression(
    const BoundExpression& source,
    const BoundInSubqueryExpression& node
) -> base::Result<BoundExpressionPtr> {
    if (node.operand == nullptr || node.query == nullptr) {
        return base::fail(clone_error("bound IN subquery has an absent operand or query"));
    }
    auto operand = clone_expression(*node.operand);
    if (!operand) {
        return base::fail(std::move(operand).error());
    }
    auto query = clone_bound_query(*node.query);
    if (!query) {
        return base::fail(std::move(query).error());
    }
    return make_bound_expression(
        source.span,
        source.data_type,
        BoundInSubqueryExpression{
            .negated = node.negated,
            .operand = std::move(operand).value(),
            .query = std::move(query).value(),
        }
    );
}

auto clone_expression(const BoundExpression& source) -> base::Result<BoundExpressionPtr> {
    if (const auto* const literal = std::get_if<BoundLiteralExpression>(&source.node);
        literal != nullptr) {
        return make_bound_expression(
            source.span,
            source.data_type,
            BoundLiteralExpression{.value = literal->value}
        );
    }
    if (const auto* const column = std::get_if<BoundColumnReference>(&source.node);
        column != nullptr) {
        return make_bound_expression(source.span, source.data_type, *column);
    }
    if (const auto* const parameter = std::get_if<BoundParameter>(&source.node);
        parameter != nullptr) {
        return make_bound_expression(source.span, source.data_type, *parameter);
    }
    if (const auto* const unary = std::get_if<BoundUnaryExpression>(&source.node);
        unary != nullptr) {
        return clone_bound_unary_expression(source, *unary);
    }
    if (const auto* const binary = std::get_if<BoundBinaryExpression>(&source.node);
        binary != nullptr) {
        return clone_bound_binary_expression(source, *binary);
    }
    if (const auto* const boolean = std::get_if<BoundBooleanExpression>(&source.node);
        boolean != nullptr) {
        return clone_bound_boolean_expression(source, *boolean);
    }
    if (const auto* const null_test = std::get_if<BoundIsNullExpression>(&source.node);
        null_test != nullptr) {
        return clone_bound_null_test_expression(source, *null_test);
    }
    if (const auto* const in_list = std::get_if<BoundInListExpression>(&source.node);
        in_list != nullptr) {
        return clone_bound_in_list_expression(source, *in_list);
    }
    if (const auto* const between = std::get_if<BoundBetweenExpression>(&source.node);
        between != nullptr) {
        return clone_bound_between_expression(source, *between);
    }
    if (const auto* const cast = std::get_if<BoundCastExpression>(&source.node); cast != nullptr) {
        return clone_bound_cast_expression(source, *cast);
    }
    if (const auto* const aggregate = std::get_if<BoundAggregateExpression>(&source.node);
        aggregate != nullptr) {
        return clone_bound_aggregate_expression(source, *aggregate);
    }
    if (const auto* const scalar = std::get_if<BoundScalarSubqueryExpression>(&source.node);
        scalar != nullptr) {
        return clone_bound_scalar_subquery_expression(source, *scalar);
    }
    if (const auto* const exists = std::get_if<BoundExistsExpression>(&source.node);
        exists != nullptr) {
        return clone_bound_exists_expression(source, *exists);
    }
    if (const auto* const in_subquery = std::get_if<BoundInSubqueryExpression>(&source.node);
        in_subquery != nullptr) {
        return clone_bound_in_subquery_expression(source, *in_subquery);
    }
    return base::fail(clone_unsupported("SELECT alias expression"));
}

auto clone_bound_table_reference(const BoundTableReference& source)
    -> base::Result<BoundTableReferencePtr>;

auto clone_bound_select_query(const BoundSelectQuery& source) -> base::Result<BoundSelectQuery>;

auto clone_bound_query(const BoundQuery& source) -> base::Result<BoundQueryPtr> {
    auto select = clone_bound_select_query(source.select);
    if (!select) {
        return base::fail(std::move(select).error());
    }
    return BoundQueryPtr{std::make_unique<BoundQuery>(source.span, std::move(select).value())};
}

auto clone_bound_join_condition(const BoundJoinTable::Condition& source)
    -> base::Result<BoundJoinTable::Condition> {
    if (std::holds_alternative<std::monostate>(source)) {
        return BoundJoinTable::Condition{std::monostate{}};
    }
    if (const auto* const on = std::get_if<BoundJoinOnCondition>(&source); on != nullptr) {
        if (on->expression == nullptr) {
            return base::fail(clone_error("bound JOIN ON has no expression"));
        }
        auto expression = clone_expression(*on->expression);
        if (!expression) {
            return base::fail(std::move(expression).error());
        }
        return BoundJoinTable::Condition{
            BoundJoinOnCondition{.expression = std::move(expression).value()},
        };
    }
    const auto* const using_condition = std::get_if<BoundJoinUsingCondition>(&source);
    if (using_condition == nullptr || using_condition->comparison == nullptr) {
        return base::fail(clone_error("bound JOIN USING has no comparison"));
    }
    auto comparison = clone_expression(*using_condition->comparison);
    if (!comparison) {
        return base::fail(std::move(comparison).error());
    }
    return BoundJoinTable::Condition{
        BoundJoinUsingCondition{
            .column_name = using_condition->column_name,
            .comparison = std::move(comparison).value(),
        },
    };
}

auto clone_bound_join_table_reference(const SourceSpan span, const BoundJoinTable& source)
    -> base::Result<BoundTableReferencePtr> {
    if (source.left == nullptr || source.right == nullptr) {
        return base::fail(clone_error("bound JOIN has an absent input"));
    }
    auto left = clone_bound_table_reference(*source.left);
    if (!left) {
        return base::fail(std::move(left).error());
    }
    auto right = clone_bound_table_reference(*source.right);
    if (!right) {
        return base::fail(std::move(right).error());
    }
    auto condition = clone_bound_join_condition(source.condition);
    if (!condition) {
        return base::fail(std::move(condition).error());
    }
    return std::make_unique<BoundTableReference>(
        span,
        BoundJoinTable{
            .kind = source.kind,
            .left = std::move(left).value(),
            .right = std::move(right).value(),
            .condition = std::move(condition).value(),
        }
    );
}

auto clone_bound_derived_table_reference(const SourceSpan span, const BoundDerivedTable& source)
    -> base::Result<BoundTableReferencePtr> {
    if (source.query == nullptr) {
        return base::fail(clone_error("bound derived table has no query"));
    }
    auto query = clone_bound_query(*source.query);
    if (!query) {
        return base::fail(std::move(query).error());
    }
    return std::make_unique<BoundTableReference>(
        span,
        BoundDerivedTable{
            .alias = source.alias,
            .columns = source.columns,
            .range_table_index = source.range_table_index,
            .query = std::move(query).value(),
        }
    );
}

auto clone_bound_table_reference(const BoundTableReference& source)
    -> base::Result<BoundTableReferencePtr> {
    if (std::holds_alternative<std::monostate>(source.node)) {
        return std::make_unique<BoundTableReference>(source.span, std::monostate{});
    }
    if (const auto* const base = std::get_if<BoundBaseTable>(&source.node); base != nullptr) {
        return std::make_unique<BoundTableReference>(source.span, *base);
    }
    if (const auto* const join = std::get_if<BoundJoinTable>(&source.node); join != nullptr) {
        return clone_bound_join_table_reference(source.span, *join);
    }
    const auto* const derived = std::get_if<BoundDerivedTable>(&source.node);
    if (derived == nullptr) {
        return base::fail(clone_error("bound derived table has no query"));
    }
    return clone_bound_derived_table_reference(source.span, *derived);
}

auto clone_optional_expression(const BoundExpressionPtr& source, const std::string_view subject)
    -> base::Result<BoundExpressionPtr> {
    if (source == nullptr) {
        return BoundExpressionPtr{};
    }
    auto copied = clone_expression(*source);
    if (!copied) {
        return base::fail(std::move(copied).error().with_context("sql", std::string{subject}));
    }
    return copied;
}

auto clone_bound_select_query(const BoundSelectQuery& source) -> base::Result<BoundSelectQuery> {
    BoundSelectQuery copied{};
    copied.distinct = source.distinct;
    copied.projection.reserve(source.projection.size());
    for (const BoundProjectionItem& item : source.projection) {
        if (item.expression == nullptr) {
            return base::fail(clone_error("bound projection has an absent expression"));
        }
        auto expression = clone_expression(*item.expression);
        if (!expression) {
            return base::fail(std::move(expression).error());
        }
        copied.projection.push_back(
            BoundProjectionItem{
                .expression = std::move(expression).value(),
                .alias = item.alias,
            }
        );
    }
    if (source.from != nullptr) {
        auto from = clone_bound_table_reference(*source.from);
        if (!from) {
            return base::fail(std::move(from).error());
        }
        copied.from = std::move(from).value();
    }
    auto filter = clone_optional_expression(source.filter, "bound filter");
    if (!filter) {
        return base::fail(std::move(filter).error());
    }
    copied.filter = std::move(filter).value();
    copied.group_by.reserve(source.group_by.size());
    for (const BoundExpressionPtr& expression : source.group_by) {
        if (expression == nullptr) {
            return base::fail(clone_error("bound GROUP BY has an absent expression"));
        }
        auto copied_expression = clone_expression(*expression);
        if (!copied_expression) {
            return base::fail(std::move(copied_expression).error());
        }
        copied.group_by.push_back(std::move(copied_expression).value());
    }
    auto having = clone_optional_expression(source.having, "bound HAVING");
    if (!having) {
        return base::fail(std::move(having).error());
    }
    copied.having = std::move(having).value();
    copied.order_by.reserve(source.order_by.size());
    for (const BoundSortItem& item : source.order_by) {
        if (item.expression == nullptr) {
            return base::fail(clone_error("bound ORDER BY has an absent expression"));
        }
        auto expression = clone_expression(*item.expression);
        if (!expression) {
            return base::fail(std::move(expression).error());
        }
        copied.order_by.push_back(
            BoundSortItem{
                .expression = std::move(expression).value(),
                .direction = item.direction,
                .null_order = item.null_order,
            }
        );
    }
    auto limit = clone_optional_expression(source.limit, "bound LIMIT");
    if (!limit) {
        return base::fail(std::move(limit).error());
    }
    copied.limit = std::move(limit).value();
    auto offset = clone_optional_expression(source.offset, "bound OFFSET");
    if (!offset) {
        return base::fail(std::move(offset).error());
    }
    copied.offset = std::move(offset).value();
    return copied;
}

}  // namespace

auto clone_bound_expression(const BoundExpression& source) -> base::Result<BoundExpressionPtr> {
    return clone_expression(source);
}

}  // namespace db::sql::detail
