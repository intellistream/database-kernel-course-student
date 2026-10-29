#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/planner/preprocess.hpp"
#include "db/sql/binder.hpp"
#include "db/sql/bound_ast.hpp"
#include "db/sql/catalog_port.hpp"
#include "db/sql/parser.hpp"
#include "db/types/coercion.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::planner {
namespace {

class FakeCatalogReadPort final : public sql::CatalogReadPort {
public:
    FakeCatalogReadPort(const std::uint64_t generation, std::vector<sql::CatalogTable> tables)
        : generation_{generation},
          tables_{std::move(tables)} {}

    auto generation() const noexcept -> std::uint64_t override {
        return generation_;
    }

    auto lookup_table(const std::string_view name) const
        -> base::Result<std::optional<sql::CatalogTable>> override {
        for (const sql::CatalogTable& table : tables_) {
            if (table.name == name) {
                return std::optional<sql::CatalogTable>{table};
            }
        }
        return std::optional<sql::CatalogTable>{};
    }

private:
    std::uint64_t generation_;
    std::vector<sql::CatalogTable> tables_;
};

auto items_table() -> sql::CatalogTable {
    return sql::CatalogTable{
        .oid = base::Oid{17U},
        .name = "items",
        .columns = {
            {.name = "id", .data_type = types::kInt64Type},
            {.name = "score", .data_type = types::kFloat32Type},
            {.name = "enabled", .data_type = types::kBoolType}
        },
    };
}

auto preprocess_sql(const std::string_view text, const FakeCatalogReadPort& catalog)
    -> base::Result<PreprocessedQuery> {
    auto source = sql::parse_sql(text);
    if (!source) {
        return base::fail(std::move(source).error());
    }
    auto bound = sql::bind_script(catalog, *source);
    if (!bound) {
        return base::fail(std::move(bound).error());
    }
    if (bound->statements.size() != 1U) {
        return base::fail(base::Error{base::ErrorCode::kPlan, "test expected one bound statement"});
    }
    sql::BoundStatement statement = std::move(bound->statements.front());
    auto* const select = std::get_if<sql::BoundSelectQuery>(&statement.node);
    if (select == nullptr) {
        return base::fail(base::Error{base::ErrorCode::kPlan, "test expected a SELECT statement"});
    }
    return preprocess_bound_query(sql::BoundQuery{statement.span, std::move(*select)});
}

auto make_literal(types::Value value) -> PreprocessedExpression {
    const types::DataType data_type = value.data_type();
    return PreprocessedExpression{
        {},
        data_type,
        QueryLiteralExpression{.value = std::move(value)},
    };
}

auto make_binary(
    const types::BinaryOperator operation,
    PreprocessedExpressionPtr left,
    PreprocessedExpressionPtr right,
    const types::DataType result_type
) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        result_type,
        QueryBinaryExpression{
            .op = operation,
            .left = std::move(left),
            .right = std::move(right),
        },
    };
}

auto make_column(const std::uint32_t column) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kInt64Type,
        QueryColumnReference{
            .range_table_index = QueryRangeTableIndex{0U},
            .column_index = QueryColumnIndex{column},
            .data_type = types::kInt64Type,
            .outer_query_levels = 0U,
        },
    };
}

auto make_boolean(
    const types::BooleanOperator operation,
    std::vector<PreprocessedExpressionPtr> operands
) -> PreprocessedExpression {
    return PreprocessedExpression{
        {},
        types::kBoolType,
        QueryBooleanExpression{
            .op = operation,
            .operands = std::move(operands),
        },
    };
}

auto expression_ptr(PreprocessedExpression expression) -> PreprocessedExpressionPtr {
    return std::make_unique<PreprocessedExpression>(std::move(expression));
}

auto expression_list(PreprocessedExpressionPtr first, PreprocessedExpressionPtr second)
    -> std::vector<PreprocessedExpressionPtr> {
    std::vector<PreprocessedExpressionPtr> expressions;
    expressions.reserve(2U);
    expressions.push_back(std::move(first));
    expressions.push_back(std::move(second));
    return expressions;
}

auto single_projection(PreprocessedExpressionPtr expression)
    -> std::vector<PreprocessedProjectionItem> {
    std::vector<PreprocessedProjectionItem> projection;
    projection.reserve(1U);
    projection.push_back(
        PreprocessedProjectionItem{
            .expression = std::move(expression),
            .alias = std::nullopt,
        }
    );
    return projection;
}

auto literal(const PreprocessedExpression& expression) -> const QueryLiteralExpression* {
    return std::get_if<QueryLiteralExpression>(&expression.node);
}

TEST(PreprocessTest, FoldsCheckedArithmeticAndLeavesDivisionForTheExecutor) {
    auto sum = fold_constant_expression(make_binary(
        types::BinaryOperator::kAdd,
        expression_ptr(make_literal(types::Value::integer(19))),
        expression_ptr(make_literal(types::Value::integer(23))),
        types::kInt64Type
    ));
    ASSERT_TRUE(sum) << sum.error().to_string();
    const auto* const folded = literal(*sum);
    ASSERT_NE(folded, nullptr);
    ASSERT_NE(folded->value.as_integer(), nullptr);
    EXPECT_EQ(*folded->value.as_integer(), 42);

    auto overflow = fold_constant_expression(make_binary(
        types::BinaryOperator::kAdd,
        expression_ptr(
            make_literal(types::Value::integer(std::numeric_limits<std::int64_t>::max()))
        ),
        expression_ptr(make_literal(types::Value::integer(1))),
        types::kInt64Type
    ));
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error().code(), base::ErrorCode::kOutOfRange);

    auto division = fold_constant_expression(make_binary(
        types::BinaryOperator::kDivide,
        expression_ptr(make_literal(types::Value::integer(1))),
        expression_ptr(make_literal(types::Value::integer(0))),
        types::kInt64Type
    ));
    ASSERT_TRUE(division) << division.error().to_string();
    EXPECT_TRUE(std::holds_alternative<QueryBinaryExpression>(division->node));
}

TEST(PreprocessTest, FoldsNullBooleanInListAndBetweenWithSqlThreeValuedLogic) {
    PreprocessedExpression boolean{
        {},
        types::kBoolType,
        QueryBooleanExpression{
            .op = types::BooleanOperator::kAnd,
            .operands = expression_list(
                expression_ptr(make_literal(types::Value::null(types::kBoolType))),
                expression_ptr(make_literal(types::Value::boolean(false)))
            ),
        },
    };
    auto folded_boolean = fold_constant_expression(std::move(boolean));
    ASSERT_TRUE(folded_boolean) << folded_boolean.error().to_string();
    const auto* const false_literal = literal(*folded_boolean);
    ASSERT_NE(false_literal, nullptr);
    ASSERT_NE(false_literal->value.as_boolean(), nullptr);
    EXPECT_FALSE(*false_literal->value.as_boolean());

    PreprocessedExpression in_list{
        {},
        types::kBoolType,
        QueryInListExpression{
            .negated = false,
            .operand = expression_ptr(make_literal(types::Value::integer(4))),
            .values = expression_list(
                expression_ptr(make_literal(types::Value::integer(3))),
                expression_ptr(make_literal(types::Value::null(types::kInt64Type)))
            ),
        },
    };
    auto folded_in_list = fold_constant_expression(std::move(in_list));
    ASSERT_TRUE(folded_in_list) << folded_in_list.error().to_string();
    const auto* const unknown_literal = literal(*folded_in_list);
    ASSERT_NE(unknown_literal, nullptr);
    EXPECT_TRUE(unknown_literal->value.is_null());
    EXPECT_EQ(unknown_literal->value.data_type(), types::kBoolType);

    PreprocessedExpression between{
        {},
        types::kBoolType,
        QueryBetweenExpression{
            .negated = false,
            .operand = expression_ptr(make_literal(types::Value::integer(4))),
            .lower = expression_ptr(make_literal(types::Value::integer(3))),
            .upper = expression_ptr(make_literal(types::Value::integer(5))),
        },
    };
    auto folded_between = fold_constant_expression(std::move(between));
    ASSERT_TRUE(folded_between) << folded_between.error().to_string();
    const auto* const true_literal = literal(*folded_between);
    ASSERT_NE(true_literal, nullptr);
    ASSERT_NE(true_literal->value.as_boolean(), nullptr);
    EXPECT_TRUE(*true_literal->value.as_boolean());
}

TEST(PreprocessTest, RetainsRuntimeArithmeticWhenBooleanConstantsAreNotTheWholeExpression) {
    const auto comparison = [] {
        return make_binary(
            types::BinaryOperator::kEqual,
            expression_ptr(make_binary(
                types::BinaryOperator::kDivide,
                expression_ptr(make_literal(types::Value::integer(1))),
                expression_ptr(make_literal(types::Value::integer(0))),
                types::kInt64Type
            )),
            expression_ptr(make_literal(types::Value::integer(1))),
            types::kBoolType
        );
    };
    const auto fold_mixed =
        [&comparison](const types::BooleanOperator operation, const bool literal) {
            return fold_constant_expression(make_boolean(
                operation,
                expression_list(
                    expression_ptr(make_literal(types::Value::boolean(literal))),
                    expression_ptr(comparison())
                )
            ));
        };

    auto retained_false = fold_mixed(types::BooleanOperator::kAnd, false);
    ASSERT_TRUE(retained_false) << retained_false.error().to_string();
    const auto* const conjunction = std::get_if<QueryBooleanExpression>(&retained_false->node);
    ASSERT_NE(conjunction, nullptr);
    ASSERT_EQ(conjunction->operands.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<QueryLiteralExpression>(conjunction->operands.at(0U)->node));
    EXPECT_TRUE(std::holds_alternative<QueryBinaryExpression>(conjunction->operands.at(1U)->node));

    auto retained_true = fold_mixed(types::BooleanOperator::kOr, true);
    ASSERT_TRUE(retained_true) << retained_true.error().to_string();
    const auto* const disjunction = std::get_if<QueryBooleanExpression>(&retained_true->node);
    ASSERT_NE(disjunction, nullptr);
    EXPECT_EQ(disjunction->operands.size(), 2U);

    auto removed_identity = fold_mixed(types::BooleanOperator::kOr, false);
    ASSERT_TRUE(removed_identity) << removed_identity.error().to_string();
    EXPECT_TRUE(std::holds_alternative<QueryBinaryExpression>(removed_identity->node));

    auto invalid_literal = fold_constant_expression(make_boolean(
        types::BooleanOperator::kAnd,
        expression_list(
            expression_ptr(comparison()),
            expression_ptr(make_literal(types::Value::integer(1)))
        )
    ));
    ASSERT_FALSE(invalid_literal);
    EXPECT_EQ(invalid_literal.error().code(), base::ErrorCode::kPlan);
    EXPECT_EQ(invalid_literal.error().message(), "boolean expression has a non-boolean literal");
}

TEST(PreprocessTest, RemovesOnlyImplicitRelabelCastsAroundNonconstantExpressions) {
    auto implicit_relabel = fold_constant_expression(
        PreprocessedExpression{
            {},
            types::kInt64Type,
            QueryCastExpression{
                .operand = expression_ptr(make_column(0U)),
                .target_type = types::kInt64Type,
                .path = types::CoercionPath{},
                .form = QueryCastForm::kImplicit,
            },
        }
    );
    ASSERT_TRUE(implicit_relabel) << implicit_relabel.error().to_string();
    EXPECT_TRUE(std::holds_alternative<QueryColumnReference>(implicit_relabel->node));

    auto explicit_relabel = fold_constant_expression(
        PreprocessedExpression{
            {},
            types::kInt64Type,
            QueryCastExpression{
                .operand = expression_ptr(make_column(0U)),
                .target_type = types::kInt64Type,
                .path = types::CoercionPath{},
                .form = QueryCastForm::kExplicit,
            },
        }
    );
    ASSERT_TRUE(explicit_relabel) << explicit_relabel.error().to_string();
    EXPECT_TRUE(std::holds_alternative<QueryCastExpression>(explicit_relabel->node));
}

TEST(PreprocessTest, CanonicalizesQualifiersWithStructuralIdentityInsteadOfSpansOrRendering) {
    auto equal_one = [] {
        return make_binary(
            types::BinaryOperator::kEqual,
            expression_ptr(make_column(0U)),
            expression_ptr(make_literal(types::Value::integer(1))),
            types::kBoolType
        );
    };
    auto equal_two = [] {
        return make_binary(
            types::BinaryOperator::kEqual,
            expression_ptr(make_column(1U)),
            expression_ptr(make_literal(types::Value::integer(2))),
            types::kBoolType
        );
    };
    auto equal_three = [] {
        return make_binary(
            types::BinaryOperator::kEqual,
            expression_ptr(make_column(1U)),
            expression_ptr(make_literal(types::Value::integer(3))),
            types::kBoolType
        );
    };

    auto first_branch = [](PreprocessedExpressionPtr first,
                           PreprocessedExpressionPtr second,
                           const sql::SourceSpan span = {}) {
        return expression_ptr(
            PreprocessedExpression{
                span,
                types::kBoolType,
                QueryBooleanExpression{
                    .op = types::BooleanOperator::kAnd,
                    .operands = expression_list(std::move(first), std::move(second)),
                },
            }
        );
    };
    PreprocessedExpression qualification{
        {.begin_byte = 7U, .end_byte = 50U},
        types::kBoolType,
        QueryBooleanExpression{
            .op = types::BooleanOperator::kOr,
            .operands = expression_list(
                first_branch(expression_ptr(equal_one()), expression_ptr(equal_two())),
                first_branch(
                    expression_ptr(equal_one()),
                    expression_ptr(equal_three()),
                    {.begin_byte = 99U, .end_byte = 130U}
                )
            ),
        },
    };

    auto result = preprocess_qualification(std::move(qualification));
    ASSERT_TRUE(result) << result.error().to_string();
    const auto* const outer_and = std::get_if<QueryBooleanExpression>(&result->node);
    ASSERT_NE(outer_and, nullptr);
    EXPECT_EQ(outer_and->op, types::BooleanOperator::kAnd);
    ASSERT_EQ(outer_and->operands.size(), 2U);
    const auto* const residual_or =
        std::get_if<QueryBooleanExpression>(&outer_and->operands.at(1U)->node);
    ASSERT_NE(residual_or, nullptr);
    EXPECT_EQ(residual_or->op, types::BooleanOperator::kOr);
}

TEST(PreprocessTest, DoesNotFactorPotentiallyErroringExpressions) {
    auto division = [] {
        return make_binary(
            types::BinaryOperator::kDivide,
            expression_ptr(make_column(0U)),
            expression_ptr(make_literal(types::Value::integer(0))),
            types::kInt64Type
        );
    };
    auto equality = [&division] {
        return make_binary(
            types::BinaryOperator::kEqual,
            expression_ptr(division()),
            expression_ptr(make_literal(types::Value::integer(1))),
            types::kBoolType
        );
    };
    auto branch = [&equality](const std::int64_t value) {
        return expression_ptr(make_boolean(
            types::BooleanOperator::kAnd,
            expression_list(
                expression_ptr(equality()),
                expression_ptr(make_binary(
                    types::BinaryOperator::kEqual,
                    expression_ptr(make_column(1U)),
                    expression_ptr(make_literal(types::Value::integer(value))),
                    types::kBoolType
                ))
            )
        ));
    };
    auto qualification =
        make_boolean(types::BooleanOperator::kOr, expression_list(branch(2), branch(3)));

    auto result = preprocess_qualification(std::move(qualification));
    ASSERT_TRUE(result) << result.error().to_string();
    const auto* const disjunction = std::get_if<QueryBooleanExpression>(&result->node);
    ASSERT_NE(disjunction, nullptr);
    EXPECT_EQ(disjunction->op, types::BooleanOperator::kOr);
}

TEST(PreprocessTest, ConvertsBoundAstOnceAndPreservesHighLevelBetweenAndInNodes) {
    const FakeCatalogReadPort catalog{101U, {items_table()}};
    auto query = preprocess_sql(
        "SELECT id FROM items WHERE id BETWEEN 1 AND 5 AND id IN (1, 2, 3)",
        catalog
    );
    ASSERT_TRUE(query) << query.error().to_string();
    ASSERT_NE(query->select.filter, nullptr);
    const auto* const conjunction =
        std::get_if<QueryBooleanExpression>(&query->select.filter->node);
    ASSERT_NE(conjunction, nullptr);
    EXPECT_EQ(conjunction->op, types::BooleanOperator::kAnd);
    ASSERT_EQ(conjunction->operands.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<QueryBetweenExpression>(conjunction->operands.at(0U)->node));
    EXPECT_TRUE(std::holds_alternative<QueryInListExpression>(conjunction->operands.at(1U)->node));
}

TEST(PreprocessTest, RejectsNonExecutableStatementKindsAtPlannerBoundary) {
    auto checkpoint =
        preprocess_bound_statement(sql::BoundStatement{{}, sql::BoundCheckpointStatement{}});

    ASSERT_FALSE(checkpoint);
    EXPECT_EQ(checkpoint.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_EQ(
        checkpoint.error().message(),
        "statement kind is not executable by planner lowering yet"
    );
    EXPECT_TRUE(checkpoint.error().contexts().empty());

    auto explain =
        preprocess_bound_statement(sql::BoundStatement{{}, sql::BoundExplainStatement{}});
    ASSERT_FALSE(explain);
    EXPECT_EQ(explain.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_EQ(explain.error().message(), checkpoint.error().message());
}

TEST(PreprocessTest, PullsUpOnlyAnIdentityDerivedTableAndMergesItsFilter) {
    const FakeCatalogReadPort catalog{102U, {items_table()}};
    auto query = preprocess_sql(
        "SELECT sub.id FROM (SELECT id, score, enabled FROM items WHERE score > 1) AS sub "
        "WHERE sub.id = 7",
        catalog
    );
    ASSERT_TRUE(query) << query.error().to_string();
    ASSERT_NE(query->select.from, nullptr);
    const auto* const base = std::get_if<PreprocessedBaseTable>(&query->select.from->node);
    ASSERT_NE(base, nullptr);
    EXPECT_EQ(base->alias, std::optional<std::string>{"sub"});
    ASSERT_NE(query->select.filter, nullptr);
    const auto* const conjunction =
        std::get_if<QueryBooleanExpression>(&query->select.filter->node);
    ASSERT_NE(conjunction, nullptr);
    EXPECT_EQ(conjunction->op, types::BooleanOperator::kAnd);
    EXPECT_EQ(conjunction->operands.size(), 2U);
    auto safety = validate_query_safety(query.value());
    EXPECT_TRUE(safety) << safety.error().to_string();
}

TEST(PreprocessTest, RetainsNonTransparentDerivedTableAndRejectsUnsupportedSafetyShapes) {
    const FakeCatalogReadPort catalog{103U, {items_table()}};
    auto retained =
        preprocess_sql("SELECT sub.id FROM (SELECT DISTINCT id FROM items) AS sub", catalog);
    ASSERT_TRUE(retained) << retained.error().to_string();
    ASSERT_NE(retained->select.from, nullptr);
    EXPECT_TRUE(std::holds_alternative<PreprocessedDerivedTable>(retained->select.from->node));

    auto projected = preprocess_sql("SELECT sub.id FROM (SELECT id FROM items) AS sub", catalog);
    ASSERT_TRUE(projected) << projected.error().to_string();
    ASSERT_NE(projected->select.from, nullptr);
    EXPECT_TRUE(std::holds_alternative<PreprocessedDerivedTable>(projected->select.from->node));

    auto renamed = preprocess_sql(
        "SELECT sub.renamed_id FROM (SELECT id AS renamed_id, score, enabled FROM items) AS sub",
        catalog
    );
    ASSERT_TRUE(renamed) << renamed.error().to_string();
    ASSERT_NE(renamed->select.from, nullptr);
    EXPECT_TRUE(std::holds_alternative<PreprocessedDerivedTable>(renamed->select.from->node));

    auto computed = preprocess_sql(
        "SELECT sub.id FROM (SELECT id + 0 AS id, score, enabled FROM items) AS sub",
        catalog
    );
    ASSERT_TRUE(computed) << computed.error().to_string();
    ASSERT_NE(computed->select.from, nullptr);
    EXPECT_TRUE(std::holds_alternative<PreprocessedDerivedTable>(computed->select.from->node));

    auto outer_join = preprocess_sql(
        "SELECT items.id FROM items LEFT JOIN items AS right_items ON items.id = right_items.id",
        catalog
    );
    ASSERT_FALSE(outer_join);
    EXPECT_EQ(outer_join.error().code(), base::ErrorCode::kUnsupported);

    auto aggregate_where = preprocess_sql("SELECT id FROM items WHERE count(*) > 0", catalog);
    ASSERT_FALSE(aggregate_where);
    EXPECT_EQ(aggregate_where.error().code(), base::ErrorCode::kUnsupported);

    auto aggregate_join_subquery = preprocess_sql(
        "SELECT count(*) FROM items AS i INNER JOIN items AS j "
        "ON EXISTS (SELECT 1 FROM items AS k WHERE k.id = i.id)",
        catalog
    );
    ASSERT_FALSE(aggregate_join_subquery);
    EXPECT_EQ(aggregate_join_subquery.error().code(), base::ErrorCode::kUnsupported);
}

TEST(PreprocessTest, RecursivelyPreprocessesSubqueriesInDepthFirstOrder) {
    const FakeCatalogReadPort catalog{104U, {items_table()}};
    auto query = preprocess_sql(
        "SELECT (SELECT 1 + 2), EXISTS (SELECT 1 WHERE 1 + 2 = 3), "
        "1 IN (SELECT 1 + 2)",
        catalog
    );
    ASSERT_TRUE(query) << query.error().to_string();
    const auto& projection = query->select.projection;
    ASSERT_EQ(projection.size(), 3U);
    const auto& scalar =
        std::get<QueryScalarSubqueryExpression>(projection.at(0U).expression->node);
    EXPECT_NE(literal(*scalar.query->select.projection.front().expression), nullptr);
    const auto& exists = std::get<QueryExistsExpression>(projection.at(1U).expression->node);
    ASSERT_NE(exists.query->select.filter, nullptr);
    EXPECT_NE(literal(*exists.query->select.filter), nullptr);
    const auto& in_subquery =
        std::get<QueryInSubqueryExpression>(projection.at(2U).expression->node);
    EXPECT_NE(literal(*in_subquery.query->select.projection.front().expression), nullptr);

    auto priority = preprocess_sql(
        "SELECT id FROM items WHERE (2147483647 * 2147483647 * 3) IN "
        "(SELECT count(*) FROM items WHERE count(*) > 0)",
        catalog
    );
    ASSERT_FALSE(priority);
    EXPECT_EQ(priority.error().code(), base::ErrorCode::kOutOfRange);
    ASSERT_EQ(priority.error().contexts().size(), 2U);
    EXPECT_EQ(priority.error().contexts()[0U].operation, "preprocess WHERE");
    EXPECT_EQ(priority.error().contexts()[1U].operation, "preprocess bound query");

    auto dead_branch = preprocess_sql(
        "SELECT id FROM items WHERE FALSE AND "
        "EXISTS (SELECT 1 FROM items WHERE count(*) > 0)",
        catalog
    );
    ASSERT_FALSE(dead_branch);
    EXPECT_EQ(dead_branch.error().code(), base::ErrorCode::kUnsupported);
}

TEST(PreprocessTest, RejectsMalformedPublicIrWithoutReenteringTheBinder) {
    PreprocessedExpression mismatched_binary = make_binary(
        types::BinaryOperator::kEqual,
        expression_ptr(make_column(0U)),
        expression_ptr(make_literal(types::Value::boolean(true))),
        types::kBoolType
    );
    PreprocessedQuery malformed_binary{
        {},
        PreprocessedSelectQuery{
            .projection = single_projection(expression_ptr(make_literal(types::Value::integer(1)))),
            .from = nullptr,
            .filter = expression_ptr(std::move(mismatched_binary)),
            .group_by = {},
            .having = nullptr,
            .order_by = {},
            .limit = nullptr,
            .offset = nullptr,
        },
    };
    auto binary_validation = validate_query_safety(malformed_binary);
    ASSERT_FALSE(binary_validation);
    EXPECT_EQ(binary_validation.error().code(), base::ErrorCode::kPlan);
    auto binary_simplification = simplify_jointree(malformed_binary);
    ASSERT_FALSE(binary_simplification);
    EXPECT_EQ(binary_simplification.error().code(), base::ErrorCode::kPlan);

    const auto mismatched_cast_path = types::find_coercion_path(
        types::kInt64Type,
        types::kFloat32Type,
        types::CoercionContext::kImplicit
    );
    ASSERT_TRUE(mismatched_cast_path);
    PreprocessedQuery malformed_cast{
        {},
        PreprocessedSelectQuery{
            .projection = single_projection(expression_ptr(
                PreprocessedExpression{
                    {},
                    types::kInt64Type,
                    QueryCastExpression{
                        .operand = expression_ptr(make_column(0U)),
                        .target_type = types::kInt64Type,
                        .path = *mismatched_cast_path,
                        .form = QueryCastForm::kImplicit,
                    },
                }
            )),
            .from = nullptr,
            .filter = nullptr,
            .group_by = {},
            .having = nullptr,
            .order_by = {},
            .limit = nullptr,
            .offset = nullptr,
        },
    };
    auto cast_validation = validate_query_safety(malformed_cast);
    ASSERT_FALSE(cast_validation);
    EXPECT_EQ(cast_validation.error().code(), base::ErrorCode::kPlan);
}

}  // namespace
}  // namespace db::planner
