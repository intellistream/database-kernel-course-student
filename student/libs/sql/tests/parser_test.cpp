#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/sql/bound_ast.hpp"
#include "db/sql/parser.hpp"

namespace db::sql {
namespace {

auto select_query(const SourceStatement& statement) -> const SourceSelectQuery& {
    const auto* const select = std::get_if<SourceSelectStatement>(&statement.node);
    EXPECT_NE(select, nullptr);
    EXPECT_NE(select->query, nullptr);
    const auto* const query = select == nullptr ? nullptr : select->query.get();
    EXPECT_NE(query, nullptr);
    const auto* const projected =
        query == nullptr ? nullptr : std::get_if<SourceSelectQuery>(&query->node);
    EXPECT_NE(projected, nullptr);
    return *projected;
}

auto only_identifier(const SourceQualifiedName& name) -> std::string_view {
    EXPECT_EQ(name.parts.size(), 1U);
    return name.parts.empty() ? std::string_view{} : std::string_view{name.parts.front().spelling};
}

auto projection_expression(const SourceProjectionItem& item) -> const SourceExpression& {
    const auto* const expression = std::get_if<SourceExpressionPtr>(&item.node);
    EXPECT_NE(expression, nullptr);
    EXPECT_NE(expression == nullptr ? nullptr : expression->get(), nullptr);
    return **expression;
}

auto join_reference(const SourceTableReference& table) -> const SourceJoinTable& {
    const auto* const join = std::get_if<SourceJoinTable>(&table.node);
    EXPECT_NE(join, nullptr);
    return *join;
}

TEST(ParserTest, ProjectsOwnedSourceAstForQueriesExpressionsAndParameters) {
    const std::string sql{
        "SELECT DISTINCT t.id AS key, CAST('1' AS INT), COUNT(DISTINCT t.v) "
        "FROM items AS t WHERE t.id BETWEEN 1 AND 9 AND t.v IN ('a', 'b') "
        "ORDER BY t.id DESC NULLS LAST LIMIT 5 OFFSET $2",
    };
    auto parsed = parse_sql(sql);
    ASSERT_TRUE(parsed) << parsed.error().to_string();
    ASSERT_EQ(parsed->statements.size(), 1U);
    const SourceScript script = std::move(parsed).value();
    EXPECT_EQ(script.source_text, sql);
    ASSERT_EQ(script.parameter_slots.size(), 1U);
    EXPECT_EQ(script.parameter_slots.front().slot, ParameterSlot{2U});
    ASSERT_EQ(script.parameter_slots.front().occurrences.size(), 1U);

    const SourceStatement& statement = script.statements.front();
    EXPECT_EQ(statement.span, (SourceSpan{.begin_byte = 0U, .end_byte = sql.size()}));
    const SourceSelectQuery& query = select_query(statement);
    EXPECT_TRUE(query.distinct);
    ASSERT_EQ(query.projection.size(), 3U);
    ASSERT_TRUE(query.projection.at(0U).alias.has_value());
    EXPECT_EQ(query.projection.at(0U).alias->spelling, "key");
    ASSERT_TRUE(std::holds_alternative<SourceExpressionPtr>(query.projection.at(0U).node));
    const auto& first_projection = std::get<SourceExpressionPtr>(query.projection.at(0U).node);
    ASSERT_NE(first_projection, nullptr);
    const auto* const first_column =
        std::get_if<SourceColumnReferenceExpression>(&first_projection->node);
    ASSERT_NE(first_column, nullptr);
    ASSERT_EQ(first_column->name.parts.size(), 2U);
    EXPECT_EQ(first_column->name.parts.at(0U).spelling, "t");
    EXPECT_EQ(first_column->name.parts.at(1U).spelling, "id");

    ASSERT_NE(query.from, nullptr);
    const auto* const from = std::get_if<SourceNamedTable>(&query.from->node);
    ASSERT_NE(from, nullptr);
    EXPECT_EQ(only_identifier(from->name), "items");
    ASSERT_TRUE(from->alias.has_value());
    EXPECT_EQ(from->alias->spelling, "t");

    ASSERT_NE(query.filter, nullptr);
    const auto* const conjunction = std::get_if<SourceBooleanExpression>(&query.filter->node);
    ASSERT_NE(conjunction, nullptr);
    EXPECT_EQ(conjunction->op, BooleanOperator::kAnd);
    ASSERT_EQ(conjunction->operands.size(), 2U);
    EXPECT_TRUE(
        std::holds_alternative<SourceBetweenExpression>(conjunction->operands.at(0U)->node)
    );
    EXPECT_TRUE(std::holds_alternative<SourceInListExpression>(conjunction->operands.at(1U)->node));

    ASSERT_EQ(query.order_by.size(), 1U);
    EXPECT_EQ(query.order_by.front().direction, SortDirection::kDescending);
    EXPECT_EQ(query.order_by.front().null_order, NullOrder::kLast);
    ASSERT_NE(query.limit, nullptr);
    EXPECT_TRUE(std::holds_alternative<SourceLiteralExpression>(query.limit->node));
    ASSERT_NE(query.offset, nullptr);
    const auto* const offset = std::get_if<SourceParameterExpression>(&query.offset->node);
    ASSERT_NE(offset, nullptr);
    EXPECT_EQ(offset->slot, ParameterSlot{2U});
}

TEST(ParserTest, ProjectsDmlDdlUtilityAndNestedStatementFamilies) {
    const std::string sql{
        "INSERT INTO dst (id, v) VALUES (1, 'a'); "
        "UPDATE dst SET v = v + 1 WHERE id = 1; "
        "DELETE FROM dst WHERE id = 2; "
        "CREATE TABLE created (id INT, c CHAR(8)) WITH (fillfactor = 80); "
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_created_id ON created USING BTREE (id); "
        "DROP INDEX IF EXISTS idx_created_id; "
        "BEGIN; EXPLAIN SELECT id FROM dst; COPY dst (id, v) FROM STDIN; COMMIT",
    };
    auto parsed = parse_sql(sql);
    ASSERT_TRUE(parsed) << parsed.error().to_string();
    const SourceScript script = std::move(parsed).value();
    ASSERT_EQ(script.statements.size(), 10U);

    const auto* const insert = std::get_if<SourceInsertStatement>(&script.statements.at(0U).node);
    ASSERT_NE(insert, nullptr);
    EXPECT_EQ(only_identifier(insert->table), "dst");
    ASSERT_EQ(insert->columns.size(), 2U);
    EXPECT_EQ(insert->columns.at(0U).spelling, "id");
    ASSERT_NE(insert->source, nullptr);
    const auto* const values = std::get_if<SourceValuesQuery>(&insert->source->node);
    ASSERT_NE(values, nullptr);
    ASSERT_EQ(values->rows.size(), 1U);

    const auto* const update = std::get_if<SourceUpdateStatement>(&script.statements.at(1U).node);
    ASSERT_NE(update, nullptr);
    ASSERT_EQ(update->assignments.size(), 1U);
    EXPECT_EQ(update->assignments.front().column.spelling, "v");
    ASSERT_NE(update->filter, nullptr);

    const auto* const delete_statement =
        std::get_if<SourceDeleteStatement>(&script.statements.at(2U).node);
    ASSERT_NE(delete_statement, nullptr);
    EXPECT_EQ(only_identifier(delete_statement->table), "dst");

    const auto* const create =
        std::get_if<SourceCreateTableStatement>(&script.statements.at(3U).node);
    ASSERT_NE(create, nullptr);
    ASSERT_EQ(create->columns.size(), 2U);
    EXPECT_EQ(create->columns.at(0U).name.spelling, "id");
    EXPECT_EQ(only_identifier(create->columns.at(0U).data_type.name), "int4");
    ASSERT_NE(create->fillfactor, nullptr);

    const auto* const index =
        std::get_if<SourceCreateIndexStatement>(&script.statements.at(4U).node);
    ASSERT_NE(index, nullptr);
    EXPECT_TRUE(index->unique);
    EXPECT_TRUE(index->if_not_exists);
    ASSERT_TRUE(index->name.has_value());
    EXPECT_EQ(index->name->spelling, "idx_created_id");
    ASSERT_TRUE(index->access_method.has_value());
    EXPECT_EQ(index->access_method->spelling, "BTREE");

    const auto* const drop = std::get_if<SourceDropIndexStatement>(&script.statements.at(5U).node);
    ASSERT_NE(drop, nullptr);
    EXPECT_TRUE(drop->if_exists);
    ASSERT_EQ(drop->indexes.size(), 1U);
    EXPECT_EQ(only_identifier(drop->indexes.front()), "idx_created_id");

    const auto* const begin =
        std::get_if<SourceTransactionStatement>(&script.statements.at(6U).node);
    ASSERT_NE(begin, nullptr);
    EXPECT_EQ(begin->kind, TransactionKind::kBegin);
    const auto* const explain = std::get_if<SourceExplainStatement>(&script.statements.at(7U).node);
    ASSERT_NE(explain, nullptr);
    ASSERT_NE(explain->statement, nullptr);
    EXPECT_TRUE(std::holds_alternative<SourceSelectStatement>(explain->statement->node));
    const auto* const copy = std::get_if<SourceCopyFromStatement>(&script.statements.at(8U).node);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(only_identifier(copy->table), "dst");
    ASSERT_EQ(copy->columns.size(), 2U);
    const auto* const commit =
        std::get_if<SourceTransactionStatement>(&script.statements.at(9U).node);
    ASSERT_NE(commit, nullptr);
    EXPECT_EQ(commit->kind, TransactionKind::kCommit);
}

TEST(ParserTest, ProjectsScalarExistsAndInSubquerySourceShapes) {
    auto parsed = parse_sql(
        "SELECT (SELECT u.id FROM users AS u WHERE u.id = outer_users.id), "
        "EXISTS (SELECT 1 FROM users AS present WHERE present.id = outer_users.id) "
        "FROM users AS outer_users "
        "WHERE outer_users.id IN (SELECT candidate.id FROM users AS candidate)"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();
    const SourceSelectQuery& select = select_query(parsed->statements.front());
    ASSERT_EQ(select.projection.size(), 2U);

    const auto* const scalar = std::get_if<SourceScalarSubqueryExpression>(
        &projection_expression(select.projection.at(0U)).node
    );
    ASSERT_NE(scalar, nullptr);
    ASSERT_NE(scalar->query, nullptr);

    const auto* const exists =
        std::get_if<SourceExistsExpression>(&projection_expression(select.projection.at(1U)).node);
    ASSERT_NE(exists, nullptr);
    EXPECT_FALSE(exists->negated);
    ASSERT_NE(exists->query, nullptr);

    ASSERT_NE(select.filter, nullptr);
    const auto* const in_subquery = std::get_if<SourceInSubqueryExpression>(&select.filter->node);
    ASSERT_NE(in_subquery, nullptr);
    EXPECT_FALSE(in_subquery->negated);
    ASSERT_NE(in_subquery->operand, nullptr);
    ASSERT_NE(in_subquery->query, nullptr);

    auto negated = parse_sql(
        "SELECT id FROM users WHERE NOT EXISTS (SELECT 1 FROM users AS present) "
        "OR id NOT IN (SELECT candidate.id FROM users AS candidate)"
    );
    ASSERT_TRUE(negated) << negated.error().to_string();
    const SourceSelectQuery& negated_select = select_query(negated->statements.front());
    ASSERT_NE(negated_select.filter, nullptr);
    const auto* const disjunction =
        std::get_if<SourceBooleanExpression>(&negated_select.filter->node);
    ASSERT_NE(disjunction, nullptr);
    ASSERT_EQ(disjunction->operands.size(), 2U);
    const auto* const negated_exists =
        std::get_if<SourceExistsExpression>(&disjunction->operands.at(0U)->node);
    ASSERT_NE(negated_exists, nullptr);
    EXPECT_TRUE(negated_exists->negated);
    const auto* const negated_in =
        std::get_if<SourceInSubqueryExpression>(&disjunction->operands.at(1U)->node);
    ASSERT_NE(negated_in, nullptr);
    EXPECT_TRUE(negated_in->negated);
}

TEST(ParserTest, ProjectsDbSourceShapesWithoutSqlparserCompatibility) {
    const std::string sql{
        "SELECT TRUE, NULL, t.*, COUNT(*), COUNT(DISTINCT t.id) "
        "FROM a AS t JOIN b USING (id, tenant) WHERE NOT t.deleted; "
        "INSERT INTO dst SELECT id FROM src; "
        "CREATE TABLE IF NOT EXISTS created (id INT) WITH (fillfactor = 80); "
        "CREATE INDEX idx_created_id ON created (id); "
        "DROP TABLE IF EXISTS created, prior; "
        "EXPLAIN UPDATE dst SET v = 1; "
        "COPY dst FROM STDIN WITH (FORMAT TEXT, DELIMITER E'\\t', NULL '\\N')",
    };
    auto parsed = parse_sql(sql);
    ASSERT_TRUE(parsed) << parsed.error().to_string();
    const SourceScript script = std::move(parsed).value();
    ASSERT_EQ(script.statements.size(), 7U);

    const SourceSelectQuery& select = select_query(script.statements.at(0U));
    ASSERT_EQ(select.projection.size(), 5U);
    const auto* const boolean_cast =
        std::get_if<SourceLiteralExpression>(&projection_expression(select.projection.at(0U)).node);
    ASSERT_NE(boolean_cast, nullptr);
    EXPECT_EQ(boolean_cast->kind, SourceLiteralKind::kBoolean);
    EXPECT_EQ(boolean_cast->text, "true");
    const auto* const null_literal =
        std::get_if<SourceLiteralExpression>(&projection_expression(select.projection.at(1U)).node);
    ASSERT_NE(null_literal, nullptr);
    EXPECT_EQ(null_literal->kind, SourceLiteralKind::kNull);
    const auto* const qualified_wildcard =
        std::get_if<SourceWildcardProjection>(&select.projection.at(2U).node);
    ASSERT_NE(qualified_wildcard, nullptr);
    EXPECT_EQ(only_identifier(qualified_wildcard->qualifier), "t");
    const auto* const count_star = std::get_if<SourceFunctionCallExpression>(
        &projection_expression(select.projection.at(3U)).node
    );
    ASSERT_NE(count_star, nullptr);
    EXPECT_TRUE(count_star->star);
    EXPECT_TRUE(count_star->arguments.empty());
    const auto* const count_distinct = std::get_if<SourceFunctionCallExpression>(
        &projection_expression(select.projection.at(4U)).node
    );
    ASSERT_NE(count_distinct, nullptr);
    EXPECT_TRUE(count_distinct->distinct);
    ASSERT_EQ(count_distinct->arguments.size(), 1U);

    ASSERT_NE(select.from, nullptr);
    const auto* const join = std::get_if<SourceJoinTable>(&select.from->node);
    ASSERT_NE(join, nullptr);
    EXPECT_EQ(join->kind, JoinKind::kInner);
    const auto* const using_condition = std::get_if<SourceJoinUsingCondition>(&join->condition);
    ASSERT_NE(using_condition, nullptr);
    ASSERT_EQ(using_condition->columns.size(), 2U);
    EXPECT_EQ(using_condition->columns.at(0U).spelling, "id");
    EXPECT_EQ(using_condition->columns.at(1U).spelling, "tenant");

    const auto* const insert = std::get_if<SourceInsertStatement>(&script.statements.at(1U).node);
    ASSERT_NE(insert, nullptr);
    ASSERT_NE(insert->source, nullptr);
    EXPECT_TRUE(std::holds_alternative<SourceSelectQuery>(insert->source->node));

    const auto* const create =
        std::get_if<SourceCreateTableStatement>(&script.statements.at(2U).node);
    ASSERT_NE(create, nullptr);
    EXPECT_TRUE(create->if_not_exists);
    ASSERT_NE(create->fillfactor, nullptr);
    const auto* const fillfactor = std::get_if<SourceLiteralExpression>(&create->fillfactor->node);
    ASSERT_NE(fillfactor, nullptr);
    EXPECT_EQ(fillfactor->kind, SourceLiteralKind::kInteger);
    EXPECT_EQ(fillfactor->text, "80");

    const auto* const index =
        std::get_if<SourceCreateIndexStatement>(&script.statements.at(3U).node);
    ASSERT_NE(index, nullptr);
    ASSERT_TRUE(index->access_method.has_value());
    EXPECT_EQ(index->access_method->spelling, "art");
    ASSERT_TRUE(index->name.has_value());
    EXPECT_EQ(index->name->spelling, "idx_created_id");

    const auto* const drop = std::get_if<SourceDropTableStatement>(&script.statements.at(4U).node);
    ASSERT_NE(drop, nullptr);
    EXPECT_TRUE(drop->if_exists);
    ASSERT_EQ(drop->tables.size(), 2U);

    const auto* const explain = std::get_if<SourceExplainStatement>(&script.statements.at(5U).node);
    ASSERT_NE(explain, nullptr);
    ASSERT_NE(explain->statement, nullptr);
    EXPECT_TRUE(std::holds_alternative<SourceUpdateStatement>(explain->statement->node));

    const auto* const copy = std::get_if<SourceCopyFromStatement>(&script.statements.at(6U).node);
    ASSERT_NE(copy, nullptr);
    ASSERT_EQ(copy->options.size(), 3U);
    EXPECT_EQ(copy->options.at(0U).name.spelling, "FORMAT");
    EXPECT_EQ(copy->options.at(1U).name.spelling, "DELIMITER");
    EXPECT_EQ(copy->options.at(2U).name.spelling, "NULL");
}

TEST(ParserTest, ProjectsJoinAndDerivedShapes) {
    const auto parsed = parse_sql(
        "SELECT * FROM a CROSS JOIN b; "
        "SELECT * FROM a, b; "
        "SELECT * FROM a JOIN b USING (id); "
        "SELECT * FROM a LEFT JOIN b ON a.id = b.id; "
        "SELECT * FROM a RIGHT JOIN b ON a.id = b.id; "
        "SELECT * FROM a FULL JOIN b ON a.id = b.id; "
        "SELECT q.id FROM (SELECT id FROM t) AS q"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();
    ASSERT_EQ(parsed->statements.size(), 7U);

    const SourceJoinTable& cross = join_reference(*select_query(parsed->statements.at(0U)).from);
    EXPECT_EQ(cross.kind, JoinKind::kCross);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(cross.condition));

    const SourceJoinTable& comma = join_reference(*select_query(parsed->statements.at(1U)).from);
    EXPECT_EQ(comma.kind, JoinKind::kCross);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(comma.condition));

    const SourceJoinTable& using_join =
        join_reference(*select_query(parsed->statements.at(2U)).from);
    EXPECT_EQ(using_join.kind, JoinKind::kInner);
    const auto* const using_condition =
        std::get_if<SourceJoinUsingCondition>(&using_join.condition);
    ASSERT_NE(using_condition, nullptr);
    ASSERT_EQ(using_condition->columns.size(), 1U);
    EXPECT_EQ(using_condition->columns.front().spelling, "id");

    constexpr std::array outer_kinds{
        JoinKind::kLeft,
        JoinKind::kRight,
        JoinKind::kFull,
    };
    for (std::size_t index = 0U; index < outer_kinds.size(); ++index) {
        const SourceJoinTable& outer =
            join_reference(*select_query(parsed->statements.at(index + 3U)).from);
        EXPECT_EQ(outer.kind, outer_kinds.at(index));
        EXPECT_TRUE(std::holds_alternative<SourceJoinOnCondition>(outer.condition));
    }

    const SourceSelectQuery& derived_select = select_query(parsed->statements.at(6U));
    ASSERT_NE(derived_select.from, nullptr);
    const auto* const derived = std::get_if<SourceDerivedTable>(&derived_select.from->node);
    ASSERT_NE(derived, nullptr);
    EXPECT_FALSE(derived->lateral);
    ASSERT_TRUE(derived->alias.has_value());
    EXPECT_EQ(derived->alias->spelling, "q");
    ASSERT_NE(derived->query, nullptr);

    const auto natural = parse_sql("SELECT * FROM a NATURAL JOIN b");
    ASSERT_FALSE(natural);
    EXPECT_EQ(natural.error().code(), base::ErrorCode::kUnsupported);
}

TEST(ParserTest, KeepsBoundAstCatalogIndependentAndMoveOnly) {
    BoundExpression expression{
        SourceSpan{.begin_byte = 7U, .end_byte = 8U},
        types::kInt64Type,
        BoundLiteralExpression{.value = types::Value::integer(42)},
    };
    EXPECT_EQ(expression.data_type, types::kInt64Type);
    const auto* const literal = std::get_if<BoundLiteralExpression>(&expression.node);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(literal->value, types::Value::integer(42));

    BoundScript script{};
    script.parameter_slots.push_back(
        BoundParameterSlot{
            .slot = ParameterSlot{2U},
            .data_type = types::kInt64Type,
            .occurrences = {
                SourceSpan{.begin_byte = 14U, .end_byte = 16U},
                SourceSpan{.begin_byte = 23U, .end_byte = 25U},
            },
        }
    );
    BoundScript moved{std::move(script)};
    ASSERT_EQ(moved.parameter_slots.size(), 1U);
    EXPECT_EQ(moved.parameter_slots.front().slot, ParameterSlot{2U});
    EXPECT_EQ(moved.parameter_slots.front().data_type, types::kInt64Type);
    EXPECT_EQ(moved.parameter_slots.front().occurrences.size(), 2U);
}

TEST(ParserTest, PreservesIdentifiersAndUsesIndependentThreadLocalArenas) {
    auto identifier_parse = parse_sql("SELECT CamelColumn FROM CamelTable");
    ASSERT_TRUE(identifier_parse) << identifier_parse.error().to_string();
    const SourceScript script = std::move(identifier_parse).value();
    const SourceSelectQuery& query = select_query(script.statements.front());
    ASSERT_NE(query.from, nullptr);
    const auto* const table = std::get_if<SourceNamedTable>(&query.from->node);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(only_identifier(table->name), "CamelTable");

    constexpr std::size_t thread_count = 8U;
    constexpr std::size_t iterations = 32U;
    std::vector<std::thread> threads;
    std::vector<std::string> failures(thread_count);
    threads.reserve(thread_count);

    for (std::size_t thread_index = 0U; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([thread_index, &failures] {
            for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
                auto parsed = parse_sql("SELECT CamelColumn FROM CamelTable");
                if (!parsed || parsed->statements.size() != 1U
                    || !std::holds_alternative<SourceSelectStatement>(
                        parsed->statements.front().node
                    )) {
                    failures.at(thread_index) =
                        parsed ? "unexpected parse result" : parsed.error().to_string();
                    return;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (const std::string& failure : failures) {
        EXPECT_TRUE(failure.empty()) << failure;
    }
}

TEST(ParserTest, RoutesDbUtilitySpellingsThroughTheNarrowCompatibilityScanner) {
    auto snapshot = parse_sql("SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION");
    ASSERT_TRUE(snapshot) << snapshot.error().to_string();
    const auto* const snapshot_setting =
        std::get_if<SourceSetTransactionStatement>(&snapshot->statements.front().node);
    ASSERT_NE(snapshot_setting, nullptr);
    EXPECT_EQ(snapshot_setting->isolation, IsolationLevel::kRepeatableRead);

    auto analyze = parse_sql("ANALYZE CamelTable");
    ASSERT_TRUE(analyze) << analyze.error().to_string();
    const auto* const analyze_statement =
        std::get_if<SourceAnalyzeStatement>(&analyze->statements.front().node);
    ASSERT_NE(analyze_statement, nullptr);
    ASSERT_TRUE(analyze_statement->table.has_value());
    EXPECT_EQ(only_identifier(*analyze_statement->table), "CamelTable");

    auto show = parse_sql("show tables;");
    ASSERT_TRUE(show) << show.error().to_string();
    EXPECT_TRUE(std::holds_alternative<SourceShowTablesStatement>(show->statements.front().node));

    auto comments = parse_sql("/* lead */ SET /* gap */ TRANSACTION ISOLATION LEVEL SERIALIZABLE;");
    ASSERT_TRUE(comments) << comments.error().to_string();
    const auto* const serializable =
        std::get_if<SourceSetTransactionStatement>(&comments->statements.front().node);
    ASSERT_NE(serializable, nullptr);
    EXPECT_EQ(serializable->isolation, IsolationLevel::kSerializable);

    const auto lookalike = parse_sql("SHOW 'TABLES'");
    ASSERT_FALSE(lookalike);
    EXPECT_EQ(lookalike.error().code(), base::ErrorCode::kUnsupported);
}

TEST(ParserTest, ExecutesSourceAstCorpusInputs) {
    constexpr std::array success_cases{
        std::string_view{""},
        std::string_view{";"},
        std::string_view{"SELECT 1; SELECT 2"},
        std::string_view{"-- lead\nSELECT 1 /* tail */;"},
        std::string_view{R"(SELECT "select" FROM "table")"},
        std::string_view{"SELECT CamelColumn FROM CamelTable"},
        std::string_view{"SELECT 名称 FROM 数据"},
        std::string_view{"SELECT * FROM a LEFT JOIN b ON a.id = b.id"},
        std::string_view{"SELECT q.id FROM (SELECT id FROM t) AS q"},
        std::string_view{"SELECT id FROM t WHERE EXISTS (SELECT 1 FROM u WHERE u.id = t.id)"},
    };
    for (const std::string_view sql : success_cases) {
        const auto parsed = parse_sql(sql);
        ASSERT_TRUE(parsed) << sql << ": " << parsed.error().to_string();
    }

    constexpr std::array parse_errors{
        "SELECT FROM",
        "SELECT 'abc",
        "SELECT (1 + 2",
        "INSERT INTO t VALUES (",
        "CREATE TABLE t (c CHAR())",
        "SELECT 1; SELECT FROM",
        "SELECT 名称 FROM",
    };
    for (const std::string_view sql : parse_errors) {
        const auto parsed = parse_sql(sql);
        ASSERT_FALSE(parsed) << sql;
        EXPECT_EQ(parsed.error().code(), base::ErrorCode::kParse) << sql;
    }
}

TEST(ParserTest, ClassifiesSyntaxFailureLimitsAndUnsupportedRawShapes) {
    const auto malformed = parse_sql("SELECT FROM");
    ASSERT_FALSE(malformed);
    EXPECT_EQ(malformed.error().code(), base::ErrorCode::kParse);
    EXPECT_NE(malformed.error().message().find("1:12"), std::string::npos);

    const auto unicode_malformed = parse_sql("SELECT 名称 FROM");
    ASSERT_FALSE(unicode_malformed);
    EXPECT_EQ(unicode_malformed.error().code(), base::ErrorCode::kParse);
    EXPECT_NE(unicode_malformed.error().message().find("1:15"), std::string::npos);

    const std::string invalid_utf8{"SELECT \xc3"};
    const auto invalid = parse_sql(invalid_utf8);
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kParse);

    constexpr std::string_view prefix{"SELECT 1"};
    constexpr std::string_view suffix{"; SELECT 2"};
    const std::string embedded_nul{
        std::string{prefix} + std::string{"\0", 1U} + std::string{suffix},
    };
    const auto nul_rejected = parse_sql(embedded_nul);
    ASSERT_FALSE(nul_rejected);
    EXPECT_EQ(nul_rejected.error().code(), base::ErrorCode::kParse);

    const auto unsupported = parse_sql("CREATE VIEW v AS SELECT 1");
    ASSERT_FALSE(unsupported);
    EXPECT_EQ(unsupported.error().code(), base::ErrorCode::kUnsupported);

    const auto like = parse_sql("SELECT id FROM t WHERE name LIKE 'a%'");
    ASSERT_FALSE(like);
    EXPECT_EQ(like.error().code(), base::ErrorCode::kUnsupported);

    constexpr std::array unsupported_raw_grammar_cases{
        "SELECT id FROM t ORDER BY id FETCH FIRST 3 ROWS WITH TIES",
        "SAVEPOINT retry_point",
        "COPY t FROM STDIN;\n1\talpha\n\\.",
    };
    for (const std::string_view sql : unsupported_raw_grammar_cases) {
        const auto rejected = parse_sql(sql);
        ASSERT_FALSE(rejected) << sql;
        EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnsupported) << sql;
    }

    const auto named_checkpoint = parse_sql("CHECKPOINT named_checkpoint");
    ASSERT_FALSE(named_checkpoint);
    EXPECT_EQ(named_checkpoint.error().code(), base::ErrorCode::kUnsupported);

    const auto source_limited = parse_sql("SELECT 1", ParseLimits{.maximum_source_bytes = 1U});
    ASSERT_FALSE(source_limited);
    EXPECT_EQ(source_limited.error().code(), base::ErrorCode::kResourceExhausted);

    const auto statements_limited =
        parse_sql("SELECT 1; SELECT 2", ParseLimits{.maximum_statements = 1U});
    ASSERT_FALSE(statements_limited);
    EXPECT_EQ(statements_limited.error().code(), base::ErrorCode::kResourceExhausted);

    const auto identifier_limited =
        parse_sql("SELECT id FROM CamelTable", ParseLimits{.maximum_identifier_bytes = 5U});
    ASSERT_FALSE(identifier_limited);
    EXPECT_EQ(identifier_limited.error().code(), base::ErrorCode::kResourceExhausted);
}

TEST(ParserTest, MapsUtf8ByteOffsetsToOneBasedCodePointLocations) {
    constexpr std::string_view sql{"SELECT 名\nX"};

    const auto start = source_position(sql, 0U);
    ASSERT_TRUE(start);
    EXPECT_EQ(*start, (SourcePosition{.line = 1U, .column = 1U}));

    const auto after_unicode = source_position(sql, 10U);
    ASSERT_TRUE(after_unicode);
    EXPECT_EQ(*after_unicode, (SourcePosition{.line = 1U, .column = 9U}));

    const auto second_line = source_position(sql, 11U);
    ASSERT_TRUE(second_line);
    EXPECT_EQ(*second_line, (SourcePosition{.line = 2U, .column = 1U}));

    const auto interior = source_position(sql, 8U);
    ASSERT_FALSE(interior);
    EXPECT_EQ(interior.error().code(), base::ErrorCode::kOutOfRange);
}

TEST(ParserTest, UnionOwnsAssociationAndClauseBoundaries) {
    auto parsed = parse_sql(
        "SELECT 1 AS n UNION ALL (SELECT 2 UNION DISTINCT SELECT 3 LIMIT 1) ORDER BY n LIMIT 2 "
        "OFFSET 1"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();
    ASSERT_EQ(parsed->statements.size(), 1U);
    const auto& statement = std::get<SourceSelectStatement>(parsed->statements[0].node);
    ASSERT_NE(statement.query, nullptr);
    const auto* set = std::get_if<SourceUnionQuery>(&statement.query->node);
    ASSERT_NE(set, nullptr);
    EXPECT_TRUE(set->all);
    ASSERT_NE(set->left, nullptr);
    ASSERT_NE(set->right, nullptr);
    EXPECT_TRUE(std::holds_alternative<SourceSelectQuery>(set->left->node));
    const auto* right = std::get_if<SourceUnionQuery>(&set->right->node);
    ASSERT_NE(right, nullptr);
    EXPECT_FALSE(right->all);
    EXPECT_NE(right->limit, nullptr);
    EXPECT_TRUE(right->order_by.empty());
    EXPECT_EQ(set->order_by.size(), 1U);
    EXPECT_NE(set->limit, nullptr);
    EXPECT_NE(set->offset, nullptr);
}

TEST(ParserTest, StaticCheckpointRecognitionRespectsTokensCommentsAndScriptBoundaries) {
    EXPECT_TRUE(is_static_checkpoint_statement("/* a */ CREATE -- b\n STATIC_CHECKPOINT; -- c"));
    for (const auto sql :
         {"SELECT 'CREATE STATIC_CHECKPOINT'",
          "SELECT 1 /* CREATE STATIC_CHECKPOINT */",
          "CREATE STATIC_CHECKPOINT_SUFFIX",
          "CREATESTATIC_CHECKPOINT",
          "CREATE STATIC_CHECKPOINT; SELECT 1",
          "CREATE STATIC_CHECKPOINT /* unterminated"}) {
        EXPECT_FALSE(is_static_checkpoint_statement(sql)) << sql;
    }
    auto script = parse_sql("SELECT 1; CREATE STATIC_CHECKPOINT");
    ASSERT_TRUE(script) << script.error().to_string();
    ASSERT_EQ(script->statements.size(), 2U);
    const auto* checkpoint = std::get_if<SourceCheckpointStatement>(&script->statements[1].node);
    ASSERT_NE(checkpoint, nullptr);
    EXPECT_TRUE(checkpoint->is_static);
}
}  // namespace
}  // namespace db::sql
