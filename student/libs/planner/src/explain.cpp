#include "db/planner/explain.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "db/base/error.hpp"

namespace db::planner {
namespace {

auto type_text(types::DataType data_type) -> std::string;
auto join_schema(const PlanSchema& outer, const PlanSchema& inner) -> PlanSchema;
void render_node(const Plan& plan, std::size_t depth, std::string& output);

auto operator_text(const types::BinaryOperator operation) -> std::string {
    switch (operation) {
        case types::BinaryOperator::kAnd:
            return "AND";
        case types::BinaryOperator::kOr:
            return "OR";
        case types::BinaryOperator::kEqual:
            return "=";
        case types::BinaryOperator::kNotEqual:
            return "<>";
        case types::BinaryOperator::kLess:
            return "<";
        case types::BinaryOperator::kLessEqual:
            return "<=";
        case types::BinaryOperator::kGreater:
            return ">";
        case types::BinaryOperator::kGreaterEqual:
            return ">=";
        case types::BinaryOperator::kAdd:
            return "+";
        case types::BinaryOperator::kSubtract:
            return "-";
        case types::BinaryOperator::kMultiply:
            return "*";
        case types::BinaryOperator::kDivide:
            return "/";
        case types::BinaryOperator::kModulo:
            return "%";
    }
    return "<unknown-op>";
}

auto aggregate_text(const PlanAggregateKind kind) -> std::string {
    switch (kind) {
        case PlanAggregateKind::kCount:
            return "COUNT";
        case PlanAggregateKind::kSum:
            return "SUM";
        case PlanAggregateKind::kAverage:
            return "AVG";
        case PlanAggregateKind::kMinimum:
            return "MIN";
        case PlanAggregateKind::kMaximum:
            return "MAX";
    }
    return "<unknown-aggregate>";
}

auto column_text(const PlanSchema& input, const PlanColumnIndex index) -> std::string {
    const auto position = static_cast<std::size_t>(index.zero_based());
    if (position < input.columns.size()) {
        return input.columns.at(position).name;
    }
    return "column_" + std::to_string(index.zero_based());
}

auto expression_text(const PlanExpression& expression, const PlanSchema& input) -> std::string;

auto expression_list_text(
    const std::vector<PlanExpressionPtr>& expressions,
    const PlanSchema& input,
    const std::string_view separator
) -> std::string {
    std::string output{};
    for (std::size_t index = 0U; index < expressions.size(); ++index) {
        if (index != 0U) {
            output.append(separator);
        }
        output.append(expression_text(*expressions.at(index), input));
    }
    return output;
}

struct ExpressionRenderer final {
    const PlanSchema& input;

    auto operator()(const PlanLiteralExpression& node) const -> std::string {
        return node.value.display();
    }

    auto operator()(const PlanInputColumnReference& node) const -> std::string {
        return column_text(input, node.column);
    }

    auto operator()(const PlanParameterReference& node) const -> std::string {
        if (node.kind == PlanParameterKind::kExternal) {
            return "$" + std::to_string(node.id.zero_based() + 1U);
        }
        return "PARAM_EXEC " + std::to_string(node.id.zero_based());
    }

    auto operator()(const PlanUnaryExpression& node) const -> std::string {
        const std::string operand = expression_text(*node.operand, input);
        if (node.op == types::UnaryOperator::kNot) {
            return "(NOT " + operand + ")";
        }
        const char operation = node.op == types::UnaryOperator::kPlus ? '+' : '-';
        return "(" + std::string{operation} + operand + ")";
    }

    auto operator()(const PlanBinaryExpression& node) const -> std::string {
        return "(" + expression_text(*node.left, input) + " " + operator_text(node.op) + " "
               + expression_text(*node.right, input) + ")";
    }

    auto operator()(const PlanBooleanExpression& node) const -> std::string {
        const std::string_view separator =
            node.op == types::BooleanOperator::kAnd ? " AND " : " OR ";
        return "(" + expression_list_text(node.operands, input, separator) + ")";
    }

    auto operator()(const PlanIsNullExpression& node) const -> std::string {
        const std::string_view negation = node.negated ? "NOT " : "";
        return "(" + expression_text(*node.operand, input) + " IS " + std::string{negation}
               + "NULL)";
    }

    auto operator()(const PlanInListExpression& node) const -> std::string {
        const std::string_view operation = node.negated ? " NOT IN (" : " IN (";
        return "(" + expression_text(*node.operand, input) + std::string{operation}
               + expression_list_text(node.values, input, ", ") + "))";
    }

    auto operator()(const PlanBetweenExpression& node) const -> std::string {
        const std::string_view operation = node.negated ? " NOT BETWEEN " : " BETWEEN ";
        return "(" + expression_text(*node.operand, input) + std::string{operation}
               + expression_text(*node.lower, input) + " AND " + expression_text(*node.upper, input)
               + ")";
    }

    auto operator()(const PlanCastExpression& node) const -> std::string {
        if (node.form == PlanCastForm::kImplicit) {
            return expression_text(*node.operand, input);
        }
        return "CAST(" + expression_text(*node.operand, input) + " AS "
               + type_text(node.target_type) + ")";
    }

    auto operator()(const PlanAggregateExpression& node) const -> std::string {
        if (node.star) {
            return aggregate_text(node.kind) + "(*)";
        }
        const std::string prefix = node.distinct ? "DISTINCT " : "";
        return aggregate_text(node.kind) + "(" + prefix
               + expression_list_text(node.arguments, input, ", ") + ")";
    }

    auto operator()(const PlanSubPlanReference& node) const -> std::string {
        return "SubPlan " + std::to_string(node.index.zero_based());
    }
};

auto expression_text(const PlanExpression& expression, const PlanSchema& input) -> std::string {
    return std::visit(ExpressionRenderer{.input = input}, expression.node);
}

auto type_text(const types::DataType data_type) -> std::string {
    switch (data_type.id().value()) {
        case types::kBoolTypeId.value():
            return "BOOL";
        case types::kInt64TypeId.value():
            return "INT";
        case types::kFloat32TypeId.value():
            return "FLOAT";
        case types::kCharTypeId.value():
            if (!data_type.modifier().is_none()) {
                return "CHAR(" + std::to_string(data_type.modifier().value()) + ")";
            }
            return "CHAR";
        case types::kTimestampTypeId.value():
            return "TIMESTAMP";
        case types::kUnknownTypeId.value():
            return "UNKNOWN";
        default:
            return "TYPE " + std::to_string(data_type.id().value());
    }
}

auto join_schema(const PlanSchema& outer, const PlanSchema& inner) -> PlanSchema {
    PlanSchema combined{};
    combined.columns.reserve(outer.columns.size() + inner.columns.size());
    combined.columns.insert(combined.columns.end(), outer.columns.begin(), outer.columns.end());
    combined.columns.insert(combined.columns.end(), inner.columns.begin(), inner.columns.end());
    return combined;
}

void append_property(
    std::string& output,
    const std::size_t depth,
    const std::string_view label,
    const std::string_view value
) {
    if (depth == 0U) {
        output.append("  ");
    } else {
        output.append(depth * 2U + 6U, ' ');
    }
    output.append(label);
    output.append(": ");
    output.append(value);
    output.push_back('\n');
}

auto relation_label(const PlanRelation& relation) -> std::string {
    return relation.alias.has_value() ? relation.name + " " + *relation.alias : relation.name;
}

auto index_conditions(const IndexScanPlan& scan) -> std::string {
    std::string output{};
    for (std::size_t position = 0U; position < scan.bounds.size(); ++position) {
        if (position != 0U) {
            output.append(" AND ");
        }
        const PlanIndexBound& bound = scan.bounds.at(position);
        const auto key_position = static_cast<std::size_t>(bound.key_index.zero_based());
        output.append(scan.index.keys.at(key_position).column_name);
        output.push_back(' ');
        output.append(operator_text(bound.comparison));
        output.push_back(' ');
        output.append(expression_text(*bound.value, scan.relation.schema));
    }
    return "(" + output + ")";
}

auto sort_keys(const std::vector<PlanSortItem>& order_by, const PlanSchema& input) -> std::string {
    std::string output{};
    for (std::size_t index = 0U; index < order_by.size(); ++index) {
        if (index != 0U) {
            output.append(", ");
        }
        const PlanSortItem& item = order_by.at(index);
        output.append(expression_text(*item.expression, input));
        if (item.direction == PlanSortDirection::kDescending) {
            output.append(" DESC");
        }
        if (item.null_order == PlanNullOrder::kFirst) {
            output.append(" NULLS FIRST");
        } else if (item.null_order == PlanNullOrder::kLast) {
            output.append(" NULLS LAST");
        }
    }
    return output;
}

auto join_conditions(
    const std::vector<PlanJoinColumnPair>& columns,
    const PlanSchema& outer,
    const PlanSchema& inner
) -> std::string {
    std::string output{};
    for (std::size_t index = 0U; index < columns.size(); ++index) {
        if (index != 0U) {
            output.append(" AND ");
        }
        const PlanJoinColumnPair& pair = columns.at(index);
        output.append("(");
        output.append(column_text(outer, pair.outer_column));
        output.append(" = ");
        output.append(column_text(inner, pair.inner_column));
        output.append(")");
    }
    return output;
}

auto modify_label(const ModifyTablePlan& modify) -> std::string {
    switch (modify.operation) {
        case ModifyOperation::kInsert:
            return "Insert on " + relation_label(modify.target);
        case ModifyOperation::kUpdate:
            return "Update on " + relation_label(modify.target);
        case ModifyOperation::kDelete:
            return "Delete on " + relation_label(modify.target);
    }
    return "ModifyTable";
}

auto node_label(const Plan& plan) -> std::string {
    return std::visit(
        [](const auto& node) -> std::string {
            using Node = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<Node, ValuesPlan>) {
                return "Values Scan";
            } else if constexpr (std::is_same_v<Node, SeqScanPlan>) {
                return "Seq Scan on " + relation_label(node.relation);
            } else if constexpr (std::is_same_v<Node, IndexScanPlan>) {
                return "Index Scan using " + node.index.name + " on "
                       + relation_label(node.relation);
            } else if constexpr (std::is_same_v<Node, ResultPlan>) {
                return "Result";
            } else if constexpr (std::is_same_v<Node, ModifyTablePlan>) {
                return modify_label(node);
            } else if constexpr (std::is_same_v<Node, SortPlan>) {
                return "Sort";
            } else if constexpr (std::is_same_v<Node, LimitPlan>) {
                return "Limit";
            } else if constexpr (std::is_same_v<Node, TopNPlan>) {
                return "Top-N Heap Sort";
            } else if constexpr (std::is_same_v<Node, AppendPlan>) {
                return "Append";
            } else if constexpr (std::is_same_v<Node, UniquePlan>) {
                return "Unique";
            } else if constexpr (std::is_same_v<Node, AggregatePlan>) {
                return "HashAggregate";
            } else if constexpr (std::is_same_v<Node, NestedLoopPlan>) {
                return "Nested Loop";
            } else if constexpr (std::is_same_v<Node, MergeJoinPlan>) {
                return "Merge Join";
            } else if constexpr (std::is_same_v<Node, HashJoinPlan>) {
                return "Hash Join";
            } else if constexpr (std::is_same_v<Node, SubqueryScanPlan>) {
                return "Subquery Scan on " + node.alias;
            } else {
                static_assert(std::is_same_v<Node, SubPlanEvalPlan>);
                return "SubPlan Eval";
            }
        },
        plan.node
    );
}

void append_optional_expression_property(
    std::string& output,
    const std::size_t depth,
    const std::string_view label,
    const PlanExpressionPtr& expression,
    const PlanSchema& input
) {
    if (expression != nullptr) {
        append_property(output, depth, label, expression_text(*expression, input));
    }
}

void append_order_property(
    std::string& output,
    const std::size_t depth,
    const std::vector<PlanSortItem>& order_by,
    const PlanSchema& input
) {
    append_property(output, depth, "Sort Key", sort_keys(order_by, input));
}

void append_aggregate_properties(
    std::string& output,
    const std::size_t depth,
    const AggregatePlan& plan
) {
    const PlanSchema& input = plan_metadata(*plan.input).output;
    if (!plan.group_by.empty()) {
        append_property(
            output,
            depth,
            "Group Key",
            expression_list_text(plan.group_by, input, ", ")
        );
    }
    if (plan.ordered_min) {
        append_property(output, depth, "Strategy", "Ordered Min");
    }
    append_optional_expression_property(output, depth, "Filter", plan.having, input);
}

void append_join_properties(
    std::string& output,
    const std::size_t depth,
    const std::vector<PlanJoinColumnPair>& columns,
    const PlanExpressionPtr& residual_filter,
    const PlanPtr& outer_plan,
    const PlanPtr& inner_plan,
    const std::string_view condition_label
) {
    const PlanSchema& outer = plan_metadata(*outer_plan).output;
    const PlanSchema& inner = plan_metadata(*inner_plan).output;
    if (!columns.empty()) {
        append_property(output, depth, condition_label, join_conditions(columns, outer, inner));
    }
    const PlanSchema joined = join_schema(outer, inner);
    append_optional_expression_property(output, depth, "Join Filter", residual_filter, joined);
}

struct PropertyRenderer final {
    std::string& output;
    std::size_t depth;

    void operator()(const ValuesPlan& unused) const {
        static_cast<void>(unused);
    }

    void operator()(const ResultPlan& unused) const {
        static_cast<void>(unused);
    }

    void operator()(const ModifyTablePlan& unused) const {
        static_cast<void>(unused);
    }

    void operator()(const LimitPlan& unused) const {
        static_cast<void>(unused);
    }

    void operator()(const AppendPlan&) const {}

    void operator()(const UniquePlan& unused) const {
        static_cast<void>(unused);
    }

    void operator()(const SeqScanPlan& node) const {
        append_optional_expression_property(
            output,
            depth,
            "Filter",
            node.filter,
            node.relation.schema
        );
    }

    void operator()(const IndexScanPlan& node) const {
        if (!node.bounds.empty()) {
            append_property(output, depth, "Index Cond", index_conditions(node));
        }
        append_optional_expression_property(
            output,
            depth,
            "Filter",
            node.filter,
            node.relation.schema
        );
    }

    void operator()(const SortPlan& node) const {
        append_order_property(output, depth, node.order_by, plan_metadata(*node.input).output);
    }

    void operator()(const TopNPlan& node) const {
        append_order_property(output, depth, node.order_by, plan_metadata(*node.input).output);
    }

    void operator()(const AggregatePlan& node) const {
        append_aggregate_properties(output, depth, node);
    }

    void operator()(const NestedLoopPlan& node) const {
        const PlanSchema joined =
            join_schema(plan_metadata(*node.outer).output, plan_metadata(*node.inner).output);
        append_optional_expression_property(output, depth, "Join Filter", node.join_filter, joined);
    }

    void operator()(const MergeJoinPlan& node) const {
        append_join_properties(
            output,
            depth,
            node.join_columns,
            node.residual_filter,
            node.outer,
            node.inner,
            "Merge Cond"
        );
    }

    void operator()(const HashJoinPlan& node) const {
        append_join_properties(
            output,
            depth,
            node.join_columns,
            node.residual_filter,
            node.outer,
            node.inner,
            "Hash Cond"
        );
    }

    void operator()(const SubqueryScanPlan& node) const {
        append_optional_expression_property(
            output,
            depth,
            "Filter",
            node.filter,
            node.metadata.output
        );
    }

    void operator()(const SubPlanEvalPlan& node) const {
        append_optional_expression_property(
            output,
            depth,
            "Filter",
            node.filter,
            plan_metadata(*node.input).output
        );
    }
};

void render_child(const PlanPtr& child, const std::size_t depth, std::string& output) {
    render_node(*child, depth + 1U, output);
}

void render_modify_child(
    const ModifyTablePlan& plan,
    const std::size_t depth,
    std::string& output
) {
    if (const auto* const query = std::get_if<PlanQueryModificationSource>(&plan.source)) {
        render_child(query->input, depth, output);
    }
}

struct ChildRenderer final {
    std::size_t depth;
    std::string& output;

    void operator()(const ValuesPlan& unused) const {
        static_cast<void>(unused);
    }

    void operator()(const SeqScanPlan& unused) const {
        static_cast<void>(unused);
    }

    void operator()(const IndexScanPlan& unused) const {
        static_cast<void>(unused);
    }

    void operator()(const ResultPlan& node) const {
        render_child(node.input, depth, output);
    }

    void operator()(const ModifyTablePlan& node) const {
        render_modify_child(node, depth, output);
    }

    void operator()(const SortPlan& node) const {
        render_child(node.input, depth, output);
    }

    void operator()(const LimitPlan& node) const {
        render_child(node.input, depth, output);
    }

    void operator()(const TopNPlan& node) const {
        render_child(node.input, depth, output);
    }

    void operator()(const AppendPlan& node) const {
        for (const auto& input : node.inputs) {
            render_child(input, depth, output);
        }
    }

    void operator()(const UniquePlan& node) const {
        render_child(node.input, depth, output);
    }

    void operator()(const AggregatePlan& node) const {
        render_child(node.input, depth, output);
    }

    void operator()(const NestedLoopPlan& node) const {
        render_child(node.outer, depth, output);
        render_child(node.inner, depth, output);
    }

    void operator()(const MergeJoinPlan& node) const {
        render_child(node.outer, depth, output);
        render_child(node.inner, depth, output);
    }

    void operator()(const HashJoinPlan& node) const {
        render_child(node.outer, depth, output);
        render_child(node.inner, depth, output);
    }

    void operator()(const SubqueryScanPlan& node) const {
        render_child(node.subquery, depth, output);
    }

    void operator()(const SubPlanEvalPlan& node) const {
        render_child(node.input, depth, output);
        for (const PlanSubPlan& subplan : node.subplans) {
            render_child(subplan.plan, depth, output);
        }
    }
};

void append_node_prefix(std::string& output, const std::size_t depth) {
    output.append(depth * 2U, ' ');
    if (depth != 0U) {
        output.append("->  ");
    }
}

void render_node(const Plan& plan, const std::size_t depth, std::string& output) {
    if (const auto* const result = std::get_if<ResultPlan>(&plan.node)) {
        render_node(*result->input, depth, output);
        return;
    }
    append_node_prefix(output, depth);
    output.append(node_label(plan));
    output.push_back('\n');
    std::visit(PropertyRenderer{.output = output, .depth = depth}, plan.node);
    std::visit(ChildRenderer{.depth = depth, .output = output}, plan.node);
}

}  // namespace

auto render_explain(const Plan& plan) -> base::Result<std::string> {
    auto valid = validate_plan(plan);
    if (!valid) {
        return base::fail(std::move(valid).error().with_context("planner", "render_explain"));
    }
    std::string output{};
    render_node(plan, 0U, output);
    return output;
}

auto render_explain(const PlannedStatement& statement) -> base::Result<std::string> {
    auto valid = validate_planned_statement(statement);
    if (!valid) {
        return base::fail(std::move(valid).error().with_context("planner", "render_explain"));
    }
    std::string output{};
    render_node(statement.root(), 0U, output);
    return output;
}

}  // namespace db::planner
