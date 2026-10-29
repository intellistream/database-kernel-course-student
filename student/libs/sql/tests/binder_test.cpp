#include <array>
#include <cstdint>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/sql/binder.hpp"
#include "db/sql/parser.hpp"
#include "db/types/coercion.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::sql {
namespace {

class FakeCatalogReadPort final : public CatalogReadPort {
public:
    FakeCatalogReadPort(const std::uint64_t generation, std::vector<CatalogTable> tables)
        : generation_{generation},
          tables_{std::move(tables)} {}

    auto generation() const noexcept -> std::uint64_t override {
        return generation_;
    }

    auto lookup_table(const std::string_view name) const
        -> base::Result<std::optional<CatalogTable>> override {
        for (const CatalogTable& table : tables_) {
            if (table.name == name) {
                return std::optional<CatalogTable>{table};
            }
        }
        return std::optional<CatalogTable>{};
    }

private:
    std::uint64_t generation_;
    std::vector<CatalogTable> tables_;
};

class FailingCatalogReadPort final : public CatalogReadPort {
public:
    auto generation() const noexcept -> std::uint64_t override {
        return 91U;
    }

    auto lookup_table(const std::string_view name) const
        -> base::Result<std::optional<CatalogTable>> override {
        static_cast<void>(name);
        return base::fail(
            base::Error{
                base::ErrorCode::kUnavailable,
                "captured catalog generation is unavailable",
            }
        );
    }
};

enum class CatalogExceptionKind : std::uint8_t {
    kStandard,
    kAllocation,
};

class ThrowingCatalogReadPort final : public CatalogReadPort {
public:
    explicit ThrowingCatalogReadPort(const CatalogExceptionKind kind)
        : kind_{kind} {}

    auto generation() const noexcept -> std::uint64_t override {
        return 92U;
    }

    auto lookup_table(const std::string_view name) const
        -> base::Result<std::optional<CatalogTable>> override {
        static_cast<void>(name);
        if (kind_ == CatalogExceptionKind::kAllocation) {
            throw std::bad_alloc{};
        }
        throw std::runtime_error{"injected catalog lookup failure"};
    }

private:
    CatalogExceptionKind kind_;
};

auto items_table() -> CatalogTable {
    return CatalogTable{
        .oid = base::Oid{17U},
        .name = "items",
        .columns = {
            {.name = "id", .data_type = types::kInt64Type},
            {.name = "label", .data_type = types::kCharType},
        },
    };
}

auto metrics_table() -> CatalogTable {
    return CatalogTable{
        .oid = base::Oid{29U},
        .name = "metrics",
        .columns = {
            {.name = "id", .data_type = types::kInt64Type},
            {.name = "score", .data_type = types::kFloat32Type},
            {.name = "enabled", .data_type = types::kBoolType},
            {
                .name = "label",
                .data_type = types::DataType{
                    types::kCharTypeId,
                    types::TypeModifier{8},
                },
            },
        },
    };
}

auto select_statement(const BoundStatement& statement) -> const BoundSelectQuery& {
    const auto* const select = std::get_if<BoundSelectQuery>(&statement.node);
    EXPECT_NE(select, nullptr);
    return *select;
}

auto column_reference(const BoundProjectionItem& item) -> const BoundColumnReference* {
    if (item.expression == nullptr) {
        return nullptr;
    }
    return std::get_if<BoundColumnReference>(&item.expression->node);
}

auto projection_expression(const BoundProjectionItem& item) -> const BoundExpression& {
    EXPECT_NE(item.expression, nullptr);
    return *item.expression;
}

auto literal_expression(const BoundExpression& expression) -> const BoundLiteralExpression* {
    return std::get_if<BoundLiteralExpression>(&expression.node);
}

auto cast_expression(const BoundExpression& expression) -> const BoundCastExpression* {
    return std::get_if<BoundCastExpression>(&expression.node);
}

auto insert_statement(const BoundStatement& statement) -> const BoundInsertStatement& {
    const auto* const insert = std::get_if<BoundInsertStatement>(&statement.node);
    EXPECT_NE(insert, nullptr);
    return *insert;
}

auto update_statement(const BoundStatement& statement) -> const BoundUpdateStatement& {
    const auto* const update = std::get_if<BoundUpdateStatement>(&statement.node);
    EXPECT_NE(update, nullptr);
    return *update;
}

auto delete_statement(const BoundStatement& statement) -> const BoundDeleteStatement& {
    const auto* const delete_statement = std::get_if<BoundDeleteStatement>(&statement.node);
    EXPECT_NE(delete_statement, nullptr);
    return *delete_statement;
}

TEST(BinderTest, ResolvesBaseColumnsAliasesAndQueryLocalRangeSlots) {
    FakeCatalogReadPort catalog{41U, {items_table()}};
    auto parsed = parse_sql(
        "SELECT t.id, t.label AS visible_label FROM items AS t; "
        "SELECT id FROM items"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    EXPECT_EQ(bound->catalog_generation, 41U);
    ASSERT_EQ(bound->statements.size(), 2U);

    const BoundSelectQuery& first = select_statement(bound->statements.at(0U));
    ASSERT_NE(first.from, nullptr);
    const auto* const first_table = std::get_if<BoundBaseTable>(&first.from->node);
    ASSERT_NE(first_table, nullptr);
    EXPECT_EQ(first_table->oid, base::Oid{17U});
    EXPECT_EQ(first_table->name, "items");
    ASSERT_TRUE(first_table->alias.has_value());
    EXPECT_EQ(*first_table->alias, "t");
    EXPECT_EQ(first_table->range_table_index, BoundRangeTableIndex{0U});
    ASSERT_EQ(first.projection.size(), 2U);
    EXPECT_FALSE(first.projection.at(0U).alias.has_value());
    ASSERT_TRUE(first.projection.at(1U).alias.has_value());
    EXPECT_EQ(*first.projection.at(1U).alias, "visible_label");

    const BoundColumnReference* const first_column = column_reference(first.projection.at(0U));
    ASSERT_NE(first_column, nullptr);
    EXPECT_EQ(first_column->range_table_index, BoundRangeTableIndex{0U});
    EXPECT_EQ(first_column->column_index, BoundColumnIndex{0U});
    EXPECT_EQ(first_column->data_type, types::kInt64Type);
    EXPECT_EQ(first_column->outer_query_levels, 0U);

    const BoundColumnReference* const second_column = column_reference(first.projection.at(1U));
    ASSERT_NE(second_column, nullptr);
    EXPECT_EQ(second_column->range_table_index, BoundRangeTableIndex{0U});
    EXPECT_EQ(second_column->column_index, BoundColumnIndex{1U});
    EXPECT_EQ(second_column->data_type, types::kCharType);

    const BoundSelectQuery& second = select_statement(bound->statements.at(1U));
    ASSERT_EQ(second.projection.size(), 1U);
    const BoundColumnReference* const second_query_column =
        column_reference(second.projection.at(0U));
    ASSERT_NE(second_query_column, nullptr);
    EXPECT_EQ(second_query_column->range_table_index, BoundRangeTableIndex{0U});
}

TEST(BinderTest, ExpandsUnqualifiedAndQualifiedWildcardsInCatalogColumnOrder) {
    FakeCatalogReadPort catalog{3U, {items_table()}};
    auto parsed = parse_sql("SELECT *, t.* FROM items AS t");
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    const BoundSelectQuery& select = select_statement(bound->statements.front());
    ASSERT_EQ(select.projection.size(), 4U);

    for (std::size_t index = 0U; index < select.projection.size(); ++index) {
        EXPECT_FALSE(select.projection.at(index).alias.has_value());
        const BoundColumnReference* const column = column_reference(select.projection.at(index));
        ASSERT_NE(column, nullptr);
        EXPECT_EQ(column->range_table_index, BoundRangeTableIndex{0U});
        EXPECT_EQ(column->column_index, BoundColumnIndex{static_cast<std::uint32_t>(index % 2U)});
    }
}

TEST(BinderTest, UsesLastPartOfQualifiedTableAndWildcardNames) {
    FakeCatalogReadPort catalog{3U, {items_table()}};
    auto parsed = parse_sql("SELECT t.* FROM public.items AS t");
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    const BoundSelectQuery& select = select_statement(bound->statements.front());
    ASSERT_NE(select.from, nullptr);
    const auto* const table = std::get_if<BoundBaseTable>(&select.from->node);
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->name, "items");
    ASSERT_TRUE(table->alias.has_value());
    EXPECT_EQ(*table->alias, "t");
    ASSERT_EQ(select.projection.size(), 2U);
    EXPECT_FALSE(select.projection.at(0U).alias.has_value());
    EXPECT_FALSE(select.projection.at(1U).alias.has_value());
}

TEST(BinderTest, PreservesDuplicateWildcardColumnsAndRejectsNamedAmbiguity) {
    CatalogTable repeated{
        .oid = base::Oid{19U},
        .name = "repeated",
        .columns = {
            {.name = "value", .data_type = types::kInt64Type},
            {.name = "value", .data_type = types::kBoolType},
        },
    };
    FakeCatalogReadPort catalog{8U, {std::move(repeated)}};

    auto wildcard_source = parse_sql("SELECT * FROM repeated");
    ASSERT_TRUE(wildcard_source) << wildcard_source.error().to_string();
    auto wildcard_bound = bind_script(catalog, *wildcard_source);
    ASSERT_TRUE(wildcard_bound) << wildcard_bound.error().to_string();
    const BoundSelectQuery& wildcard = select_statement(wildcard_bound->statements.front());
    ASSERT_EQ(wildcard.projection.size(), 2U);
    EXPECT_FALSE(wildcard.projection.at(0U).alias.has_value());
    EXPECT_FALSE(wildcard.projection.at(1U).alias.has_value());

    auto named_source = parse_sql("SELECT value FROM repeated");
    ASSERT_TRUE(named_source) << named_source.error().to_string();
    const auto named_bound = bind_script(catalog, *named_source);
    ASSERT_FALSE(named_bound);
    EXPECT_EQ(named_bound.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(named_bound.error().message().find("ambiguous"), std::string::npos);
}

TEST(BinderTest, PreservesAmbiguityAfterIndexingARepeatedlyReadWideScope) {
    CatalogTable repeated{
        .oid = base::Oid{20U},
        .name = "wide_repeated",
        .columns = {},
    };
    repeated.columns.reserve(16U);
    for (std::size_t index = 0U; index < 14U; ++index) {
        repeated.columns.push_back(
            CatalogColumn{
                .name = "c" + std::to_string(index),
                .data_type = types::kInt64Type,
            }
        );
    }
    repeated.columns.push_back({.name = "repeated", .data_type = types::kInt64Type});
    repeated.columns.push_back({.name = "repeated", .data_type = types::kBoolType});
    FakeCatalogReadPort catalog{9U, {std::move(repeated)}};

    auto indexed_source = parse_sql("SELECT c0, c1, c13 FROM wide_repeated");
    ASSERT_TRUE(indexed_source) << indexed_source.error().to_string();
    auto indexed_bound = bind_script(catalog, *indexed_source);
    ASSERT_TRUE(indexed_bound) << indexed_bound.error().to_string();
    const BoundSelectQuery& indexed = select_statement(indexed_bound->statements.front());
    const BoundColumnReference* const c13 = column_reference(indexed.projection.at(2U));
    ASSERT_NE(c13, nullptr);
    EXPECT_EQ(c13->column_index, BoundColumnIndex{13U});

    auto source = parse_sql("SELECT c0, c1, repeated FROM wide_repeated");
    ASSERT_TRUE(source) << source.error().to_string();
    const auto bound = bind_script(catalog, *source);
    ASSERT_FALSE(bound);
    EXPECT_EQ(bound.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(bound.error().message().find("ambiguous"), std::string::npos);
}

TEST(BinderTest, ResolvesCrossTableScopeAndRejectsAmbiguousColumnReferences) {
    CatalogTable other{
        .oid = base::Oid{18U},
        .name = "other",
        .columns = {
            {.name = "id", .data_type = types::kInt64Type},
            {.name = "enabled", .data_type = types::kBoolType},
        },
    };
    FakeCatalogReadPort catalog{10U, {items_table(), std::move(other)}};

    auto qualified_source = parse_sql(
        "SELECT a.id, b.enabled, a.*, b.* "
        "FROM items AS a, other AS b"
    );
    ASSERT_TRUE(qualified_source) << qualified_source.error().to_string();
    auto qualified_bound = bind_script(catalog, *qualified_source);
    ASSERT_TRUE(qualified_bound) << qualified_bound.error().to_string();
    const BoundSelectQuery& qualified = select_statement(qualified_bound->statements.front());
    ASSERT_NE(qualified.from, nullptr);
    ASSERT_TRUE(std::holds_alternative<BoundJoinTable>(qualified.from->node));
    ASSERT_EQ(qualified.projection.size(), 6U);
    for (const BoundProjectionItem& item : qualified.projection) {
        EXPECT_FALSE(item.alias.has_value());
    }
    const BoundColumnReference* const left_id = column_reference(qualified.projection.at(0U));
    const BoundColumnReference* const right_enabled = column_reference(qualified.projection.at(1U));
    ASSERT_NE(left_id, nullptr);
    ASSERT_NE(right_enabled, nullptr);
    EXPECT_EQ(left_id->range_table_index, BoundRangeTableIndex{0U});
    EXPECT_EQ(right_enabled->range_table_index, BoundRangeTableIndex{1U});

    auto ambiguous_source = parse_sql("SELECT id FROM items AS a, other AS b");
    ASSERT_TRUE(ambiguous_source) << ambiguous_source.error().to_string();
    const auto ambiguous_bound = bind_script(catalog, *ambiguous_source);
    ASSERT_FALSE(ambiguous_bound);
    EXPECT_EQ(ambiguous_bound.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(ambiguous_bound.error().message().find("ambiguous"), std::string::npos);
}

TEST(BinderTest, RejectsDuplicateVisibleTableNamesBeforeProjection) {
    FakeCatalogReadPort catalog{10U, {items_table()}};

    auto duplicate_source = parse_sql("SELECT * FROM items AS t, items AS t");
    ASSERT_TRUE(duplicate_source) << duplicate_source.error().to_string();
    const auto duplicate_bound = bind_script(catalog, *duplicate_source);
    ASSERT_FALSE(duplicate_bound);
    EXPECT_EQ(duplicate_bound.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(duplicate_bound.error().message().find("appears more than once"), std::string::npos);
}

TEST(BinderTest, EnforcesAliasVisibilityAndStableNameErrors) {
    FakeCatalogReadPort catalog{2U, {items_table()}};

    auto hidden_name_source = parse_sql("SELECT items.id FROM items AS t");
    ASSERT_TRUE(hidden_name_source) << hidden_name_source.error().to_string();
    const auto hidden_name = bind_script(catalog, *hidden_name_source);
    ASSERT_FALSE(hidden_name);
    EXPECT_EQ(hidden_name.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(hidden_name.error().message().find("items.id"), std::string::npos);

    auto missing_name_source = parse_sql("SELECT missing FROM items");
    ASSERT_TRUE(missing_name_source) << missing_name_source.error().to_string();
    const auto missing_name = bind_script(catalog, *missing_name_source);
    ASSERT_FALSE(missing_name);
    EXPECT_EQ(missing_name.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(missing_name.error().message().find("missing"), std::string::npos);

    auto missing_wildcard_source = parse_sql("SELECT missing.* FROM items");
    ASSERT_TRUE(missing_wildcard_source) << missing_wildcard_source.error().to_string();
    const auto missing_wildcard = bind_script(catalog, *missing_wildcard_source);
    ASSERT_FALSE(missing_wildcard);
    EXPECT_EQ(missing_wildcard.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_NE(missing_wildcard.error().message().find("empty SELECT list"), std::string::npos);

    auto source_without_from = parse_sql("SELECT *");
    ASSERT_TRUE(source_without_from) << source_without_from.error().to_string();
    const auto without_from = bind_script(catalog, *source_without_from);
    ASSERT_FALSE(without_from);
    EXPECT_EQ(without_from.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_NE(without_from.error().message().find("empty SELECT list"), std::string::npos);
}

TEST(BinderTest, BindsLiteralUnaryAndArithmeticProjectionExpressions) {
    FakeCatalogReadPort catalog{13U, {}};
    auto parsed = parse_sql("SELECT 7 AS integer_value, 1.5, TRUE, NULL, 'plain', -4, 2 + 3 * 4");
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    const BoundSelectQuery& select = select_statement(bound->statements.front());
    ASSERT_EQ(select.projection.size(), 7U);

    ASSERT_TRUE(select.projection.at(0U).alias.has_value());
    EXPECT_EQ(*select.projection.at(0U).alias, "integer_value");
    const BoundExpression& integer = projection_expression(select.projection.at(0U));
    EXPECT_EQ(integer.data_type, types::kInt64Type);
    const BoundLiteralExpression* const integer_literal = literal_expression(integer);
    ASSERT_NE(integer_literal, nullptr);
    ASSERT_NE(integer_literal->value.as_integer(), nullptr);
    EXPECT_EQ(*integer_literal->value.as_integer(), 7);

    const BoundExpression& floating = projection_expression(select.projection.at(1U));
    EXPECT_EQ(floating.data_type, types::kFloat32Type);
    const BoundLiteralExpression* const float_literal = literal_expression(floating);
    ASSERT_NE(float_literal, nullptr);
    ASSERT_NE(float_literal->value.as_float(), nullptr);
    EXPECT_FLOAT_EQ(*float_literal->value.as_float(), 1.5F);

    const BoundExpression& boolean = projection_expression(select.projection.at(2U));
    EXPECT_EQ(boolean.data_type, types::kBoolType);
    const BoundLiteralExpression* const boolean_literal = literal_expression(boolean);
    ASSERT_NE(boolean_literal, nullptr);
    ASSERT_NE(boolean_literal->value.as_boolean(), nullptr);
    EXPECT_TRUE(*boolean_literal->value.as_boolean());

    const BoundExpression& null = projection_expression(select.projection.at(3U));
    EXPECT_EQ(null.data_type, types::kCharType);
    const BoundLiteralExpression* const null_literal = literal_expression(null);
    ASSERT_NE(null_literal, nullptr);
    EXPECT_TRUE(null_literal->value.is_null());
    EXPECT_EQ(null_literal->value.data_type(), types::kCharType);

    const BoundExpression& string = projection_expression(select.projection.at(4U));
    EXPECT_EQ(string.data_type, types::kCharType);
    const BoundLiteralExpression* const string_literal = literal_expression(string);
    ASSERT_NE(string_literal, nullptr);
    ASSERT_NE(string_literal->value.as_string(), nullptr);
    EXPECT_EQ(*string_literal->value.as_string(), "plain");
    EXPECT_EQ(string_literal->value.data_type(), types::kCharType);

    const BoundExpression& unary = projection_expression(select.projection.at(5U));
    EXPECT_EQ(unary.data_type, types::kInt64Type);
    const BoundLiteralExpression* const unary_literal = literal_expression(unary);
    ASSERT_NE(unary_literal, nullptr);
    ASSERT_NE(unary_literal->value.as_integer(), nullptr);
    EXPECT_EQ(*unary_literal->value.as_integer(), -4);

    const BoundExpression& arithmetic = projection_expression(select.projection.at(6U));
    EXPECT_EQ(arithmetic.data_type, types::kInt64Type);
    const auto* const add = std::get_if<BoundBinaryExpression>(&arithmetic.node);
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(add->op, types::BinaryOperator::kAdd);
    ASSERT_NE(add->right, nullptr);
    const auto* const multiply = std::get_if<BoundBinaryExpression>(&add->right->node);
    ASSERT_NE(multiply, nullptr);
    EXPECT_EQ(multiply->op, types::BinaryOperator::kMultiply);
}

TEST(BinderTest, BindsProjectionAndFilterExpressionsWithCommonTypeCoercion) {
    FakeCatalogReadPort catalog{14U, {metrics_table()}};
    auto parsed = parse_sql(
        "SELECT id + score AS total FROM metrics; "
        "SELECT id FROM metrics WHERE id BETWEEN '1' AND 9; "
        "SELECT id FROM metrics WHERE id IN (1.0, 3); "
        "SELECT id FROM metrics WHERE NOT enabled OR id = '7'"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 4U);

    const BoundSelectQuery& arithmetic_query = select_statement(bound->statements.at(0U));
    ASSERT_EQ(arithmetic_query.projection.size(), 1U);
    ASSERT_TRUE(arithmetic_query.projection.front().alias.has_value());
    EXPECT_EQ(*arithmetic_query.projection.front().alias, "total");
    const BoundExpression& arithmetic = projection_expression(arithmetic_query.projection.front());
    EXPECT_EQ(arithmetic.data_type, types::kFloat32Type);
    const auto* const add = std::get_if<BoundBinaryExpression>(&arithmetic.node);
    ASSERT_NE(add, nullptr);
    ASSERT_NE(add->left, nullptr);
    const BoundCastExpression* const left_cast = cast_expression(*add->left);
    ASSERT_NE(left_cast, nullptr);
    EXPECT_EQ(left_cast->target_type, types::kFloat32Type);
    EXPECT_EQ(left_cast->form, BoundCastForm::kImplicit);
    const auto implicit_path = types::find_coercion_path(
        types::kInt64Type,
        types::kFloat32Type,
        types::CoercionContext::kImplicit
    );
    ASSERT_TRUE(implicit_path);
    EXPECT_EQ(left_cast->path, *implicit_path);

    const BoundSelectQuery& between_query = select_statement(bound->statements.at(1U));
    ASSERT_NE(between_query.filter, nullptr);
    EXPECT_EQ(between_query.filter->data_type, types::kBoolType);
    const auto* const between = std::get_if<BoundBetweenExpression>(&between_query.filter->node);
    ASSERT_NE(between, nullptr);
    ASSERT_NE(between->operand, nullptr);
    ASSERT_NE(between->lower, nullptr);
    ASSERT_NE(between->upper, nullptr);
    EXPECT_EQ(between->operand->data_type, types::kInt64Type);
    EXPECT_EQ(between->lower->data_type, types::kInt64Type);
    EXPECT_EQ(between->upper->data_type, types::kInt64Type);
    const BoundLiteralExpression* const lower = literal_expression(*between->lower);
    ASSERT_NE(lower, nullptr);
    ASSERT_NE(lower->value.as_integer(), nullptr);
    EXPECT_EQ(*lower->value.as_integer(), 1);

    const BoundSelectQuery& in_query = select_statement(bound->statements.at(2U));
    ASSERT_NE(in_query.filter, nullptr);
    const auto* const in_list = std::get_if<BoundInListExpression>(&in_query.filter->node);
    ASSERT_NE(in_list, nullptr);
    ASSERT_NE(in_list->operand, nullptr);
    ASSERT_EQ(in_list->values.size(), 2U);
    EXPECT_EQ(in_list->operand->data_type, types::kFloat32Type);
    EXPECT_NE(cast_expression(*in_list->operand), nullptr);
    EXPECT_EQ(in_list->values.at(0U)->data_type, types::kFloat32Type);
    EXPECT_EQ(in_list->values.at(1U)->data_type, types::kFloat32Type);

    const BoundSelectQuery& boolean_query = select_statement(bound->statements.at(3U));
    ASSERT_NE(boolean_query.filter, nullptr);
    const auto* const disjunction =
        std::get_if<BoundBooleanExpression>(&boolean_query.filter->node);
    ASSERT_NE(disjunction, nullptr);
    EXPECT_EQ(disjunction->op, types::BooleanOperator::kOr);
    ASSERT_EQ(disjunction->operands.size(), 2U);
    ASSERT_NE(disjunction->operands.at(0U), nullptr);
    ASSERT_NE(disjunction->operands.at(1U), nullptr);
    const auto* const negation =
        std::get_if<BoundBooleanExpression>(&disjunction->operands.at(0U)->node);
    ASSERT_NE(negation, nullptr);
    EXPECT_EQ(negation->op, types::BooleanOperator::kNot);
    const auto* const comparison =
        std::get_if<BoundBinaryExpression>(&disjunction->operands.at(1U)->node);
    ASSERT_NE(comparison, nullptr);
    ASSERT_NE(comparison->right, nullptr);
    EXPECT_EQ(comparison->right->data_type, types::kInt64Type);
}

TEST(BinderTest, BindsExplicitCastsAndPreservesNonConstantCastForm) {
    FakeCatalogReadPort catalog{15U, {metrics_table()}};
    auto parsed = parse_sql(
        "SELECT CAST('7' AS INT), CAST(TRUE AS INT), CAST(1.5 AS INT), "
        "CAST(12345 AS CHAR(3)), TIMESTAMP '2026-08-12 14:00:00' FROM metrics; "
        "SELECT CAST(id AS FLOAT) AS decimal_id FROM metrics"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 2U);

    const BoundSelectQuery& constants = select_statement(bound->statements.at(0U));
    ASSERT_EQ(constants.projection.size(), 5U);
    for (std::size_t index = 0U; index < 3U; ++index) {
        const BoundExpression& expression = projection_expression(constants.projection.at(index));
        EXPECT_EQ(expression.data_type, types::kInt64Type);
        const BoundLiteralExpression* const literal = literal_expression(expression);
        ASSERT_NE(literal, nullptr);
        ASSERT_NE(literal->value.as_integer(), nullptr);
    }
    const BoundLiteralExpression* const integer_literal =
        literal_expression(projection_expression(constants.projection.at(0U)));
    ASSERT_NE(integer_literal, nullptr);
    EXPECT_EQ(*integer_literal->value.as_integer(), 7);
    const BoundLiteralExpression* const boolean_literal =
        literal_expression(projection_expression(constants.projection.at(1U)));
    ASSERT_NE(boolean_literal, nullptr);
    EXPECT_EQ(*boolean_literal->value.as_integer(), 1);
    const BoundLiteralExpression* const rounded_literal =
        literal_expression(projection_expression(constants.projection.at(2U)));
    ASSERT_NE(rounded_literal, nullptr);
    EXPECT_EQ(*rounded_literal->value.as_integer(), 2);

    const BoundExpression& character = projection_expression(constants.projection.at(3U));
    EXPECT_EQ(character.data_type, (types::DataType{types::kCharTypeId, types::TypeModifier{3}}));
    const BoundLiteralExpression* const character_literal = literal_expression(character);
    ASSERT_NE(character_literal, nullptr);
    ASSERT_NE(character_literal->value.as_string(), nullptr);
    EXPECT_EQ(*character_literal->value.as_string(), "123");

    const BoundExpression& timestamp = projection_expression(constants.projection.at(4U));
    EXPECT_EQ(
        timestamp.data_type,
        (types::DataType{
            types::kTimestampTypeId,
            types::TypeModifier{static_cast<std::int32_t>(types::kMaxTimestampPrecision)},
        })
    );
    const BoundLiteralExpression* const timestamp_literal = literal_expression(timestamp);
    ASSERT_NE(timestamp_literal, nullptr);
    EXPECT_NE(timestamp_literal->value.as_timestamp(), nullptr);

    const BoundSelectQuery& column_cast = select_statement(bound->statements.at(1U));
    ASSERT_EQ(column_cast.projection.size(), 1U);
    ASSERT_TRUE(column_cast.projection.front().alias.has_value());
    EXPECT_EQ(*column_cast.projection.front().alias, "decimal_id");
    const BoundExpression& cast_expression_node =
        projection_expression(column_cast.projection.front());
    EXPECT_EQ(cast_expression_node.data_type, types::kFloat32Type);
    const BoundCastExpression* const cast = cast_expression(cast_expression_node);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->target_type, types::kFloat32Type);
    EXPECT_EQ(cast->form, BoundCastForm::kExplicit);
    const auto explicit_path = types::find_coercion_path(
        types::kInt64Type,
        types::kFloat32Type,
        types::CoercionContext::kExplicit
    );
    ASSERT_TRUE(explicit_path);
    EXPECT_EQ(cast->path, *explicit_path);
}

TEST(BinderTest, BindsDistinctOrderAliasesAndRowCounts) {
    FakeCatalogReadPort catalog{18U, {metrics_table()}};
    auto parsed = parse_sql(
        "SELECT DISTINCT id + 1 AS next_id, 'ready' AS state "
        "FROM metrics WHERE enabled "
        "ORDER BY next_id DESC NULLS LAST, state ASC "
        "LIMIT '5' OFFSET 2"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    const BoundSelectQuery& select = select_statement(bound->statements.front());
    EXPECT_TRUE(select.distinct);
    ASSERT_EQ(select.projection.size(), 2U);
    ASSERT_TRUE(select.projection.at(0U).alias.has_value());
    EXPECT_EQ(*select.projection.at(0U).alias, "next_id");
    ASSERT_TRUE(select.projection.at(1U).alias.has_value());
    EXPECT_EQ(*select.projection.at(1U).alias, "state");
    EXPECT_EQ(projection_expression(select.projection.at(1U)).data_type, types::kCharType);

    ASSERT_NE(select.filter, nullptr);
    EXPECT_EQ(select.filter->data_type, types::kBoolType);

    ASSERT_EQ(select.order_by.size(), 2U);
    EXPECT_EQ(select.order_by.at(0U).direction, SortDirection::kDescending);
    EXPECT_EQ(select.order_by.at(0U).null_order, NullOrder::kLast);
    ASSERT_NE(select.order_by.at(0U).expression, nullptr);
    const auto* const next_id =
        std::get_if<BoundBinaryExpression>(&select.order_by.at(0U).expression->node);
    ASSERT_NE(next_id, nullptr);
    ASSERT_NE(next_id->left, nullptr);
    EXPECT_NE(next_id->left.get(), select.projection.at(0U).expression.get());
    EXPECT_NE(next_id->left.get(), select.order_by.at(0U).expression.get());
    EXPECT_EQ(select.order_by.at(1U).direction, SortDirection::kAscending);
    EXPECT_EQ(select.order_by.at(1U).null_order, NullOrder::kDefault);
    ASSERT_NE(select.order_by.at(1U).expression, nullptr);
    EXPECT_EQ(select.order_by.at(1U).expression->data_type, types::kCharType);
    const BoundLiteralExpression* const state_literal =
        literal_expression(*select.order_by.at(1U).expression);
    ASSERT_NE(state_literal, nullptr);
    ASSERT_NE(state_literal->value.as_string(), nullptr);
    EXPECT_EQ(*state_literal->value.as_string(), "ready");

    ASSERT_NE(select.limit, nullptr);
    EXPECT_EQ(select.limit->data_type, types::kInt64Type);
    const BoundLiteralExpression* const limit_literal = literal_expression(*select.limit);
    ASSERT_NE(limit_literal, nullptr);
    ASSERT_NE(limit_literal->value.as_integer(), nullptr);
    EXPECT_EQ(*limit_literal->value.as_integer(), 5);
    ASSERT_NE(select.offset, nullptr);
    EXPECT_EQ(select.offset->data_type, types::kInt64Type);
    const BoundLiteralExpression* const offset_literal = literal_expression(*select.offset);
    ASSERT_NE(offset_literal, nullptr);
    ASSERT_NE(offset_literal->value.as_integer(), nullptr);
    EXPECT_EQ(*offset_literal->value.as_integer(), 2);
}

TEST(BinderTest, DeepClonesSubqueryProjectionAliases) {
    FakeCatalogReadPort catalog{19U, {items_table(), metrics_table()}};
    auto parsed = parse_sql(
        "SELECT (SELECT m.score FROM metrics AS m WHERE m.id = outer_items.id) "
        "AS correlated_score FROM items AS outer_items ORDER BY correlated_score"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    const BoundSelectQuery& select = select_statement(bound->statements.front());
    ASSERT_EQ(select.projection.size(), 1U);
    ASSERT_EQ(select.order_by.size(), 1U);

    const auto* const projected = std::get_if<BoundScalarSubqueryExpression>(
        &projection_expression(select.projection.front()).node
    );
    ASSERT_NE(projected, nullptr);
    ASSERT_NE(projected->query, nullptr);
    ASSERT_NE(select.order_by.front().expression, nullptr);
    const auto* const ordered =
        std::get_if<BoundScalarSubqueryExpression>(&select.order_by.front().expression->node);
    ASSERT_NE(ordered, nullptr);
    ASSERT_NE(ordered->query, nullptr);
    EXPECT_NE(select.projection.front().expression.get(), select.order_by.front().expression.get());
    EXPECT_NE(projected->query.get(), ordered->query.get());

    ASSERT_NE(ordered->query->select.filter, nullptr);
    const auto* const comparison =
        std::get_if<BoundBinaryExpression>(&ordered->query->select.filter->node);
    ASSERT_NE(comparison, nullptr);
    ASSERT_NE(comparison->right, nullptr);
    const auto* const outer_column = std::get_if<BoundColumnReference>(&comparison->right->node);
    ASSERT_NE(outer_column, nullptr);
    EXPECT_EQ(outer_column->outer_query_levels, 1U);
}

TEST(BinderTest, AllowsEarlierProjectionAliasesButNotWhereAliases) {
    FakeCatalogReadPort catalog{20U, {metrics_table()}};
    auto chained_projection =
        parse_sql("SELECT id + 1 AS next_id, next_id + 1 AS after_next FROM metrics");
    ASSERT_TRUE(chained_projection) << chained_projection.error().to_string();

    auto chained_bound = bind_script(catalog, *chained_projection);
    ASSERT_TRUE(chained_bound) << chained_bound.error().to_string();
    const BoundSelectQuery& select = select_statement(chained_bound->statements.front());
    ASSERT_EQ(select.projection.size(), 2U);
    const auto* const after_next =
        std::get_if<BoundBinaryExpression>(&select.projection.at(1U).expression->node);
    ASSERT_NE(after_next, nullptr);
    ASSERT_NE(after_next->left, nullptr);
    const auto* const chained_alias = std::get_if<BoundBinaryExpression>(&after_next->left->node);
    ASSERT_NE(chained_alias, nullptr);
    EXPECT_NE(after_next->left.get(), select.projection.at(0U).expression.get());

    auto where_alias = parse_sql("SELECT id + 1 AS next_id FROM metrics WHERE next_id > 1");
    ASSERT_TRUE(where_alias) << where_alias.error().to_string();
    const auto where_bound = bind_script(catalog, *where_alias);
    ASSERT_FALSE(where_bound);
    EXPECT_EQ(where_bound.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(where_bound.error().message().find("next_id"), std::string::npos);
}

TEST(BinderTest, RejectsAmbiguousOrderAliasesAndInvalidRowCounts) {
    FakeCatalogReadPort catalog{19U, {metrics_table()}};

    auto colliding_alias_source = parse_sql("SELECT id AS label FROM metrics ORDER BY label");
    ASSERT_TRUE(colliding_alias_source) << colliding_alias_source.error().to_string();
    const auto colliding_alias = bind_script(catalog, *colliding_alias_source);
    ASSERT_FALSE(colliding_alias);
    EXPECT_EQ(colliding_alias.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(colliding_alias.error().message().find("ambiguous"), std::string::npos);

    auto duplicate_alias_source =
        parse_sql("SELECT id AS same, score AS same FROM metrics ORDER BY same");
    ASSERT_TRUE(duplicate_alias_source) << duplicate_alias_source.error().to_string();
    const auto duplicate_alias = bind_script(catalog, *duplicate_alias_source);
    ASSERT_FALSE(duplicate_alias);
    EXPECT_EQ(duplicate_alias.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(duplicate_alias.error().message().find("ambiguous"), std::string::npos);

    // UNKNOWN output resolution replaces each projection expression. Duplicate
    // aliases must remain ambiguous without leaving the transient alias scope
    // pointing at a replaced expression.
    auto duplicate_unknown_alias_source =
        parse_sql("SELECT NULL AS same, NULL AS same FROM metrics ORDER BY same");
    ASSERT_TRUE(duplicate_unknown_alias_source)
        << duplicate_unknown_alias_source.error().to_string();
    const auto duplicate_unknown_alias = bind_script(catalog, *duplicate_unknown_alias_source);
    ASSERT_FALSE(duplicate_unknown_alias);
    EXPECT_EQ(duplicate_unknown_alias.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(duplicate_unknown_alias.error().message().find("ambiguous"), std::string::npos);

    auto missing_order_source = parse_sql("SELECT id FROM metrics ORDER BY missing");
    ASSERT_TRUE(missing_order_source) << missing_order_source.error().to_string();
    const auto missing_order = bind_script(catalog, *missing_order_source);
    ASSERT_FALSE(missing_order);
    EXPECT_EQ(missing_order.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(missing_order.error().message().find("missing"), std::string::npos);

    auto floating_limit_source = parse_sql("SELECT id FROM metrics LIMIT 1.5");
    ASSERT_TRUE(floating_limit_source) << floating_limit_source.error().to_string();
    const auto floating_limit = bind_script(catalog, *floating_limit_source);
    ASSERT_FALSE(floating_limit);
    EXPECT_EQ(floating_limit.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(floating_limit.error().message().find("cannot coerce"), std::string::npos);

    auto boolean_offset_source = parse_sql("SELECT id FROM metrics OFFSET enabled");
    ASSERT_TRUE(boolean_offset_source) << boolean_offset_source.error().to_string();
    const auto boolean_offset = bind_script(catalog, *boolean_offset_source);
    ASSERT_FALSE(boolean_offset);
    EXPECT_EQ(boolean_offset.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(boolean_offset.error().message().find("cannot coerce"), std::string::npos);
}

TEST(BinderTest, BindsAggregatesGroupByAndHavingWithDbResultTypes) {
    FakeCatalogReadPort catalog{20U, {metrics_table()}};
    auto parsed = parse_sql(
        "SELECT id AS grouping_key, COUNT(*) AS rows, COUNT(DISTINCT label) AS labels, "
        "SUM(id) AS total, AVG(score) AS average, MIN(label) AS first_label, MAX(score) AS top "
        "FROM metrics GROUP BY grouping_key HAVING COUNT(*) > 1 ORDER BY total"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    const BoundSelectQuery& select = select_statement(bound->statements.front());
    ASSERT_EQ(select.projection.size(), 7U);

    const auto* const count_star = std::get_if<BoundAggregateExpression>(
        &projection_expression(select.projection.at(1U)).node
    );
    ASSERT_NE(count_star, nullptr);
    EXPECT_EQ(count_star->kind, BoundAggregateKind::kCount);
    EXPECT_TRUE(count_star->star);
    EXPECT_FALSE(count_star->distinct);
    EXPECT_TRUE(count_star->arguments.empty());
    EXPECT_EQ(projection_expression(select.projection.at(1U)).data_type, types::kInt64Type);

    const auto* const count_distinct = std::get_if<BoundAggregateExpression>(
        &projection_expression(select.projection.at(2U)).node
    );
    ASSERT_NE(count_distinct, nullptr);
    EXPECT_EQ(count_distinct->kind, BoundAggregateKind::kCount);
    EXPECT_TRUE(count_distinct->distinct);
    ASSERT_EQ(count_distinct->arguments.size(), 1U);
    EXPECT_EQ(
        count_distinct->arguments.front()->data_type,
        (types::DataType{types::kCharTypeId, types::TypeModifier{8}})
    );

    const auto* const sum = std::get_if<BoundAggregateExpression>(
        &projection_expression(select.projection.at(3U)).node
    );
    ASSERT_NE(sum, nullptr);
    EXPECT_EQ(sum->kind, BoundAggregateKind::kSum);
    EXPECT_EQ(projection_expression(select.projection.at(3U)).data_type, types::kInt64Type);

    const auto* const average = std::get_if<BoundAggregateExpression>(
        &projection_expression(select.projection.at(4U)).node
    );
    ASSERT_NE(average, nullptr);
    EXPECT_EQ(average->kind, BoundAggregateKind::kAverage);
    EXPECT_EQ(projection_expression(select.projection.at(4U)).data_type, types::kFloat32Type);

    const auto* const minimum = std::get_if<BoundAggregateExpression>(
        &projection_expression(select.projection.at(5U)).node
    );
    ASSERT_NE(minimum, nullptr);
    EXPECT_EQ(minimum->kind, BoundAggregateKind::kMinimum);
    EXPECT_EQ(
        projection_expression(select.projection.at(5U)).data_type,
        (types::DataType{types::kCharTypeId, types::TypeModifier{8}})
    );

    const auto* const maximum = std::get_if<BoundAggregateExpression>(
        &projection_expression(select.projection.at(6U)).node
    );
    ASSERT_NE(maximum, nullptr);
    EXPECT_EQ(maximum->kind, BoundAggregateKind::kMaximum);
    EXPECT_EQ(projection_expression(select.projection.at(6U)).data_type, types::kFloat32Type);

    ASSERT_EQ(select.group_by.size(), 1U);
    const auto* const grouped_alias =
        std::get_if<BoundColumnReference>(&select.group_by.front()->node);
    ASSERT_NE(grouped_alias, nullptr);
    EXPECT_EQ(grouped_alias->column_index, BoundColumnIndex{0U});

    ASSERT_NE(select.having, nullptr);
    EXPECT_EQ(select.having->data_type, types::kBoolType);
    const auto* const having = std::get_if<BoundBinaryExpression>(&select.having->node);
    ASSERT_NE(having, nullptr);
    ASSERT_NE(having->left, nullptr);
    EXPECT_TRUE(std::holds_alternative<BoundAggregateExpression>(having->left->node));

    ASSERT_EQ(select.order_by.size(), 1U);
    const auto* const ordered_alias =
        std::get_if<BoundAggregateExpression>(&select.order_by.front().expression->node);
    ASSERT_NE(ordered_alias, nullptr);
    EXPECT_EQ(ordered_alias->kind, BoundAggregateKind::kSum);
}

TEST(BinderTest, EnforcesAggregateArityStarNestingAndTypes) {
    FakeCatalogReadPort catalog{21U, {metrics_table()}};

    const auto assert_unsupported =
        [&catalog](const std::string_view sql, const std::string_view text) {
            auto parsed = parse_sql(sql);
            ASSERT_TRUE(parsed) << parsed.error().to_string();
            const auto bound = bind_script(catalog, *parsed);
            ASSERT_FALSE(bound);
            EXPECT_EQ(bound.error().code(), base::ErrorCode::kUnsupported);
            EXPECT_NE(bound.error().message().find(text), std::string::npos);
        };

    assert_unsupported("SELECT SUM(*) FROM metrics", "only count(*)");
    assert_unsupported("SELECT COUNT() FROM metrics", "exactly one argument");
    assert_unsupported("SELECT SUM(id, score) FROM metrics", "exactly one argument");
    assert_unsupported("SELECT SUM(enabled) FROM metrics", "numeric argument");
    assert_unsupported("SELECT SUM(COUNT(*)) FROM metrics", "cannot be nested");
    assert_unsupported("SELECT arbitrary(id) FROM metrics", "function: arbitrary");

    auto unknowns = parse_sql("SELECT SUM(NULL), AVG(NULL), MIN(NULL), MAX(NULL) FROM metrics");
    ASSERT_TRUE(unknowns) << unknowns.error().to_string();
    auto bound = bind_script(catalog, *unknowns);
    ASSERT_TRUE(bound) << bound.error().to_string();
    const BoundSelectQuery& select = select_statement(bound->statements.front());
    ASSERT_EQ(select.projection.size(), 4U);
    EXPECT_EQ(projection_expression(select.projection.at(0U)).data_type, types::kInt64Type);
    EXPECT_EQ(projection_expression(select.projection.at(1U)).data_type, types::kFloat32Type);
    EXPECT_EQ(projection_expression(select.projection.at(2U)).data_type, types::kCharType);
    EXPECT_EQ(projection_expression(select.projection.at(3U)).data_type, types::kCharType);
}

TEST(BinderTest, RejectsInvalidAndIncompatibleExpressionCoercions) {
    FakeCatalogReadPort catalog{16U, {metrics_table()}};

    auto invalid_literal_source = parse_sql("SELECT 'not-a-number' + 1");
    ASSERT_TRUE(invalid_literal_source) << invalid_literal_source.error().to_string();
    const auto invalid_literal = bind_script(catalog, *invalid_literal_source);
    ASSERT_FALSE(invalid_literal);
    EXPECT_EQ(invalid_literal.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(invalid_literal.error().message().find("invalid literal"), std::string::npos);

    auto unknown_comparison_source = parse_sql("SELECT 'left' = 'right'");
    ASSERT_TRUE(unknown_comparison_source) << unknown_comparison_source.error().to_string();
    const auto unknown_comparison = bind_script(catalog, *unknown_comparison_source);
    ASSERT_TRUE(unknown_comparison) << unknown_comparison.error().to_string();
    const BoundSelectQuery& unknown_query =
        select_statement(unknown_comparison->statements.front());
    const BoundExpression& unknown_expression =
        projection_expression(unknown_query.projection.front());
    const auto* const unknown_binary = std::get_if<BoundBinaryExpression>(&unknown_expression.node);
    ASSERT_NE(unknown_binary, nullptr);
    ASSERT_NE(unknown_binary->left, nullptr);
    ASSERT_NE(unknown_binary->right, nullptr);
    EXPECT_EQ(unknown_binary->left->data_type, types::kCharType);
    EXPECT_EQ(unknown_binary->right->data_type, types::kCharType);

    auto incompatible_source = parse_sql("SELECT id + enabled FROM metrics");
    ASSERT_TRUE(incompatible_source) << incompatible_source.error().to_string();
    const auto incompatible = bind_script(catalog, *incompatible_source);
    ASSERT_FALSE(incompatible);
    EXPECT_EQ(incompatible.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(incompatible.error().message().find("cannot coerce"), std::string::npos);

    auto non_boolean_filter_source = parse_sql("SELECT id FROM metrics WHERE id");
    ASSERT_TRUE(non_boolean_filter_source) << non_boolean_filter_source.error().to_string();
    const auto non_boolean_filter = bind_script(catalog, *non_boolean_filter_source);
    ASSERT_FALSE(non_boolean_filter);
    EXPECT_EQ(non_boolean_filter.error().code(), base::ErrorCode::kBind);

    auto unsupported_type_source = parse_sql("SELECT CAST(id AS DATE) FROM metrics");
    ASSERT_TRUE(unsupported_type_source) << unsupported_type_source.error().to_string();
    const auto unsupported_type = bind_script(catalog, *unsupported_type_source);
    ASSERT_FALSE(unsupported_type);
    EXPECT_EQ(unsupported_type.error().code(), base::ErrorCode::kUnsupported);
}

TEST(BinderTest, BindsJoinConditionsWithAJoinLocalScope) {
    CatalogTable prior{
        .oid = base::Oid{33U},
        .name = "prior",
        .columns = {{.name = "prior_only", .data_type = types::kInt64Type}},
    };
    FakeCatalogReadPort catalog{5U, {items_table(), std::move(prior)}};

    auto join_source = parse_sql(
        "SELECT a.id FROM items AS a JOIN items AS b "
        "ON a.id + b.id > 0"
    );
    ASSERT_TRUE(join_source) << join_source.error().to_string();
    auto join_bound = bind_script(catalog, *join_source);
    ASSERT_TRUE(join_bound) << join_bound.error().to_string();
    const BoundSelectQuery& select = select_statement(join_bound->statements.front());
    ASSERT_NE(select.from, nullptr);
    const auto* const join = std::get_if<BoundJoinTable>(&select.from->node);
    ASSERT_NE(join, nullptr);
    EXPECT_EQ(join->kind, JoinKind::kInner);
    const auto* const on = std::get_if<BoundJoinOnCondition>(&join->condition);
    ASSERT_NE(on, nullptr);
    ASSERT_NE(on->expression, nullptr);
    EXPECT_EQ(on->expression->data_type, types::kBoolType);
    const auto* const greater = std::get_if<BoundBinaryExpression>(&on->expression->node);
    ASSERT_NE(greater, nullptr);
    EXPECT_EQ(greater->op, types::BinaryOperator::kGreater);
    ASSERT_NE(greater->left, nullptr);
    const auto* const addition = std::get_if<BoundBinaryExpression>(&greater->left->node);
    ASSERT_NE(addition, nullptr);
    ASSERT_NE(addition->left, nullptr);
    ASSERT_NE(addition->right, nullptr);
    const auto* const left = std::get_if<BoundColumnReference>(&addition->left->node);
    const auto* const right = std::get_if<BoundColumnReference>(&addition->right->node);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->range_table_index, BoundRangeTableIndex{0U});
    EXPECT_EQ(right->range_table_index, BoundRangeTableIndex{1U});

    constexpr std::array outer_kinds{
        JoinKind::kLeft,
        JoinKind::kRight,
        JoinKind::kFull,
    };
    constexpr std::array outer_keywords{
        std::string_view{"LEFT"},
        std::string_view{"RIGHT"},
        std::string_view{"FULL"},
    };
    for (std::size_t index = 0U; index < outer_kinds.size(); ++index) {
        std::string outer_sql{"SELECT a.id FROM items AS a "};
        outer_sql.append(outer_keywords.at(index));
        outer_sql.append(" JOIN items AS b ON a.id = b.id");
        auto outer_join_source = parse_sql(outer_sql);
        ASSERT_TRUE(outer_join_source) << outer_join_source.error().to_string();
        auto outer_join_bound = bind_script(catalog, *outer_join_source);
        ASSERT_TRUE(outer_join_bound) << outer_join_bound.error().to_string();
        const BoundSelectQuery& outer_select =
            select_statement(outer_join_bound->statements.front());
        ASSERT_NE(outer_select.from, nullptr);
        const auto* const outer_join = std::get_if<BoundJoinTable>(&outer_select.from->node);
        ASSERT_NE(outer_join, nullptr);
        EXPECT_EQ(outer_join->kind, outer_kinds.at(index));
        EXPECT_TRUE(std::holds_alternative<BoundJoinOnCondition>(outer_join->condition));
    }

    auto sibling_leak = parse_sql(
        "SELECT * FROM prior AS p, items AS a JOIN items AS b "
        "ON p.prior_only = a.id"
    );
    ASSERT_TRUE(sibling_leak) << sibling_leak.error().to_string();
    const auto leaked_bound = bind_script(catalog, *sibling_leak);
    ASSERT_FALSE(leaked_bound);
    EXPECT_EQ(leaked_bound.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(leaked_bound.error().message().find("p.prior_only"), std::string::npos);
}

TEST(BinderTest, BindsCrossCommaAndSingleColumnUsingJoins) {
    FakeCatalogReadPort catalog{21U, {items_table(), metrics_table()}};
    auto parsed = parse_sql(
        "SELECT a.id, b.enabled FROM items AS a CROSS JOIN metrics AS b; "
        "SELECT a.id, b.id FROM items AS a, metrics AS b; "
        "SELECT * FROM items AS a JOIN metrics AS b USING (id)"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 3U);

    for (std::size_t index = 0U; index < 2U; ++index) {
        const BoundSelectQuery& select = select_statement(bound->statements.at(index));
        ASSERT_NE(select.from, nullptr);
        const auto* const join = std::get_if<BoundJoinTable>(&select.from->node);
        ASSERT_NE(join, nullptr);
        EXPECT_EQ(join->kind, JoinKind::kCross);
        EXPECT_TRUE(std::holds_alternative<std::monostate>(join->condition));
    }

    const BoundSelectQuery& using_select = select_statement(bound->statements.at(2U));
    ASSERT_NE(using_select.from, nullptr);
    const auto* const using_join = std::get_if<BoundJoinTable>(&using_select.from->node);
    ASSERT_NE(using_join, nullptr);
    EXPECT_EQ(using_join->kind, JoinKind::kInner);
    const auto* const using_condition =
        std::get_if<BoundJoinUsingCondition>(&using_join->condition);
    ASSERT_NE(using_condition, nullptr);
    EXPECT_EQ(using_condition->column_name, "id");
    ASSERT_NE(using_condition->comparison, nullptr);
    const auto* const equality =
        std::get_if<BoundBinaryExpression>(&using_condition->comparison->node);
    ASSERT_NE(equality, nullptr);
    EXPECT_EQ(equality->op, types::BinaryOperator::kEqual);
    ASSERT_NE(equality->left, nullptr);
    ASSERT_NE(equality->right, nullptr);
    const auto* const left = std::get_if<BoundColumnReference>(&equality->left->node);
    const auto* const right = std::get_if<BoundColumnReference>(&equality->right->node);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(left->range_table_index, BoundRangeTableIndex{0U});
    EXPECT_EQ(right->range_table_index, BoundRangeTableIndex{1U});
    ASSERT_EQ(using_select.projection.size(), 6U);
}

TEST(BinderTest, BindsDerivedTablesAndIsolatesTheirScopes) {
    FakeCatalogReadPort catalog{22U, {items_table()}};
    auto parsed = parse_sql(
        "SELECT q.item_id, q.label "
        "FROM (SELECT id AS item_id, label FROM items) AS q; "
        "SELECT * FROM (SELECT id, id FROM items) AS duplicate_columns"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 2U);

    const BoundSelectQuery& named = select_statement(bound->statements.at(0U));
    ASSERT_NE(named.from, nullptr);
    const auto* const derived = std::get_if<BoundDerivedTable>(&named.from->node);
    ASSERT_NE(derived, nullptr);
    EXPECT_EQ(derived->alias, "q");
    EXPECT_EQ(derived->range_table_index, BoundRangeTableIndex{0U});
    ASSERT_EQ(derived->columns.size(), 2U);
    EXPECT_EQ(derived->columns.at(0U).name, "item_id");
    EXPECT_EQ(derived->columns.at(0U).data_type, types::kInt64Type);
    EXPECT_EQ(derived->columns.at(1U).name, "label");
    EXPECT_EQ(derived->columns.at(1U).data_type, types::kCharType);
    ASSERT_NE(derived->query, nullptr);
    ASSERT_NE(derived->query->select.from, nullptr);
    const auto* const inner_table = std::get_if<BoundBaseTable>(&derived->query->select.from->node);
    ASSERT_NE(inner_table, nullptr);
    EXPECT_EQ(inner_table->range_table_index, BoundRangeTableIndex{0U});

    const BoundSelectQuery& duplicates = select_statement(bound->statements.at(1U));
    ASSERT_NE(duplicates.from, nullptr);
    const auto* const duplicate_derived = std::get_if<BoundDerivedTable>(&duplicates.from->node);
    ASSERT_NE(duplicate_derived, nullptr);
    ASSERT_EQ(duplicate_derived->columns.size(), 2U);
    EXPECT_EQ(duplicate_derived->columns.at(0U).name, "id");
    EXPECT_EQ(duplicate_derived->columns.at(1U).name, "id");
    ASSERT_EQ(duplicates.projection.size(), 2U);

    auto duplicate_named_reference = parse_sql(
        "SELECT duplicate_columns.id "
        "FROM (SELECT id, id FROM items) AS duplicate_columns"
    );
    ASSERT_TRUE(duplicate_named_reference) << duplicate_named_reference.error().to_string();
    const auto ambiguous_duplicate = bind_script(catalog, *duplicate_named_reference);
    ASSERT_FALSE(ambiguous_duplicate);
    EXPECT_EQ(ambiguous_duplicate.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(ambiguous_duplicate.error().message().find("ambiguous"), std::string::npos);

    auto unknown = parse_sql("SELECT q.state FROM (SELECT 'ready' AS state) AS q");
    ASSERT_TRUE(unknown) << unknown.error().to_string();
    auto unknown_bound = bind_script(catalog, *unknown);
    ASSERT_TRUE(unknown_bound) << unknown_bound.error().to_string();
    const BoundSelectQuery& unknown_select = select_statement(unknown_bound->statements.front());
    ASSERT_NE(unknown_select.from, nullptr);
    const auto* const unknown_derived = std::get_if<BoundDerivedTable>(&unknown_select.from->node);
    ASSERT_NE(unknown_derived, nullptr);
    ASSERT_EQ(unknown_derived->columns.size(), 1U);
    EXPECT_EQ(unknown_derived->columns.front().name, "state");
    EXPECT_EQ(unknown_derived->columns.front().data_type, types::kCharType);

    auto sibling_reference = parse_sql(
        "SELECT * FROM items AS outer_items, "
        "(SELECT outer_items.id) AS q"
    );
    ASSERT_TRUE(sibling_reference) << sibling_reference.error().to_string();
    const auto isolated = bind_script(catalog, *sibling_reference);
    ASSERT_FALSE(isolated);
    EXPECT_EQ(isolated.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(isolated.error().message().find("outer_items.id"), std::string::npos);
}

TEST(BinderTest, NamesDerivedOutputsThroughDenseRangeTablePositions) {
    FakeCatalogReadPort catalog{24U, {items_table(), metrics_table()}};
    auto parsed = parse_sql(
        "SELECT q.* FROM ("
        "SELECT m2.enabled, i1.label, m1.score, i2.id, m2.id, i1.id "
        "FROM metrics AS m1 "
        "JOIN items AS i1 ON m1.id = i1.id "
        "JOIN items AS i2 ON i1.id = i2.id "
        "JOIN metrics AS m2 ON i2.id = m2.id"
        ") AS q"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    const BoundSelectQuery& select = select_statement(bound->statements.front());
    ASSERT_NE(select.from, nullptr);
    const auto* const derived = std::get_if<BoundDerivedTable>(&select.from->node);
    ASSERT_NE(derived, nullptr);
    ASSERT_EQ(derived->columns.size(), 6U);
    constexpr std::array<std::string_view, 6U> expected_names{
        "enabled",
        "label",
        "score",
        "id",
        "id",
        "id",
    };
    for (std::size_t index = 0U; index < expected_names.size(); ++index) {
        EXPECT_EQ(derived->columns.at(index).name, expected_names.at(index));
    }
    ASSERT_EQ(select.projection.size(), derived->columns.size());
}

TEST(BinderTest, RejectsUnsupportedDerivedAndJoinFormsAtTheirBoundaries) {
    FakeCatalogReadPort catalog{23U, {items_table(), metrics_table()}};

    auto multiple_using = parse_sql("SELECT * FROM items AS a JOIN metrics AS b USING (id, label)");
    ASSERT_TRUE(multiple_using) << multiple_using.error().to_string();
    const auto multiple_using_bound = bind_script(catalog, *multiple_using);
    ASSERT_FALSE(multiple_using_bound);
    EXPECT_EQ(multiple_using_bound.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_NE(multiple_using_bound.error().message().find("multiple columns"), std::string::npos);

    auto lateral = parse_sql("SELECT * FROM LATERAL (SELECT id FROM items) AS q");
    ASSERT_TRUE(lateral) << lateral.error().to_string();
    const auto lateral_bound = bind_script(catalog, *lateral);
    ASSERT_FALSE(lateral_bound);
    EXPECT_EQ(lateral_bound.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_NE(lateral_bound.error().message().find("LATERAL"), std::string::npos);

    auto unnamed_expression = parse_sql("SELECT * FROM (SELECT id + 1 FROM items) AS q");
    ASSERT_TRUE(unnamed_expression) << unnamed_expression.error().to_string();
    const auto unnamed_expression_bound = bind_script(catalog, *unnamed_expression);
    ASSERT_FALSE(unnamed_expression_bound);
    EXPECT_EQ(unnamed_expression_bound.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_NE(unnamed_expression_bound.error().message().find("explicit alias"), std::string::npos);
}

TEST(BinderTest, BindsScalarExistsAndInSubqueriesWithCorrelation) {
    FakeCatalogReadPort catalog{24U, {items_table(), metrics_table()}};
    auto parsed = parse_sql(
        "SELECT (SELECT m.score FROM metrics AS m WHERE m.id = outer_items.id) "
        "AS correlated_score FROM items AS outer_items "
        "WHERE NOT EXISTS (SELECT 1 FROM metrics AS present "
        "WHERE present.id = outer_items.id) "
        "AND outer_items.id NOT IN (SELECT scores.score FROM metrics AS scores)"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    const BoundSelectQuery& select = select_statement(bound->statements.front());
    ASSERT_EQ(select.projection.size(), 1U);
    const auto* const scalar = std::get_if<BoundScalarSubqueryExpression>(
        &projection_expression(select.projection.front()).node
    );
    ASSERT_NE(scalar, nullptr);
    ASSERT_NE(scalar->query, nullptr);
    ASSERT_EQ(scalar->query->select.projection.size(), 1U);
    const BoundExpression& scalar_value =
        projection_expression(scalar->query->select.projection.front());
    EXPECT_EQ(scalar_value.data_type, types::kFloat32Type);
    ASSERT_NE(scalar->query->select.filter, nullptr);
    const auto* const scalar_comparison =
        std::get_if<BoundBinaryExpression>(&scalar->query->select.filter->node);
    ASSERT_NE(scalar_comparison, nullptr);
    ASSERT_NE(scalar_comparison->left, nullptr);
    ASSERT_NE(scalar_comparison->right, nullptr);
    const auto* const scalar_local =
        std::get_if<BoundColumnReference>(&scalar_comparison->left->node);
    const auto* const scalar_outer =
        std::get_if<BoundColumnReference>(&scalar_comparison->right->node);
    ASSERT_NE(scalar_local, nullptr);
    ASSERT_NE(scalar_outer, nullptr);
    EXPECT_EQ(scalar_local->outer_query_levels, 0U);
    EXPECT_EQ(scalar_outer->outer_query_levels, 1U);

    ASSERT_NE(select.filter, nullptr);
    const auto* const conjunction = std::get_if<BoundBooleanExpression>(&select.filter->node);
    ASSERT_NE(conjunction, nullptr);
    EXPECT_EQ(conjunction->op, types::BooleanOperator::kAnd);
    ASSERT_EQ(conjunction->operands.size(), 2U);

    const auto* const exists =
        std::get_if<BoundExistsExpression>(&conjunction->operands.at(0U)->node);
    ASSERT_NE(exists, nullptr);
    EXPECT_TRUE(exists->negated);
    ASSERT_NE(exists->query, nullptr);
    ASSERT_NE(exists->query->select.filter, nullptr);
    const auto* const exists_comparison =
        std::get_if<BoundBinaryExpression>(&exists->query->select.filter->node);
    ASSERT_NE(exists_comparison, nullptr);
    ASSERT_NE(exists_comparison->right, nullptr);
    const auto* const exists_outer =
        std::get_if<BoundColumnReference>(&exists_comparison->right->node);
    ASSERT_NE(exists_outer, nullptr);
    EXPECT_EQ(exists_outer->outer_query_levels, 1U);

    const auto* const in_subquery =
        std::get_if<BoundInSubqueryExpression>(&conjunction->operands.at(1U)->node);
    ASSERT_NE(in_subquery, nullptr);
    EXPECT_TRUE(in_subquery->negated);
    ASSERT_NE(in_subquery->operand, nullptr);
    EXPECT_EQ(in_subquery->operand->data_type, types::kFloat32Type);
    EXPECT_NE(cast_expression(*in_subquery->operand), nullptr);
    ASSERT_NE(in_subquery->query, nullptr);
    ASSERT_EQ(in_subquery->query->select.projection.size(), 1U);
    EXPECT_EQ(
        projection_expression(in_subquery->query->select.projection.front()).data_type,
        types::kFloat32Type
    );
}

TEST(BinderTest, PreservesNestedCorrelationLevelsAndOuterDerivedVisibility) {
    FakeCatalogReadPort catalog{25U, {items_table(), metrics_table()}};
    auto nested = parse_sql(
        "SELECT (SELECT (SELECT outer_items.id) FROM metrics AS middle_metrics LIMIT 1) "
        "AS nested_id FROM items AS outer_items"
    );
    ASSERT_TRUE(nested) << nested.error().to_string();
    auto nested_bound = bind_script(catalog, *nested);
    ASSERT_TRUE(nested_bound) << nested_bound.error().to_string();
    const BoundSelectQuery& nested_select = select_statement(nested_bound->statements.front());
    const auto* const first_subquery = std::get_if<BoundScalarSubqueryExpression>(
        &projection_expression(nested_select.projection.front()).node
    );
    ASSERT_NE(first_subquery, nullptr);
    ASSERT_NE(first_subquery->query, nullptr);
    const auto* const second_subquery = std::get_if<BoundScalarSubqueryExpression>(
        &projection_expression(first_subquery->query->select.projection.front()).node
    );
    ASSERT_NE(second_subquery, nullptr);
    ASSERT_NE(second_subquery->query, nullptr);
    const BoundColumnReference* const nested_outer =
        column_reference(second_subquery->query->select.projection.front());
    ASSERT_NE(nested_outer, nullptr);
    EXPECT_EQ(nested_outer->outer_query_levels, 2U);

    auto derived = parse_sql(
        "SELECT (SELECT q.outer_id FROM (SELECT outer_items.id AS outer_id) AS q) "
        "AS derived_value FROM items AS outer_items"
    );
    ASSERT_TRUE(derived) << derived.error().to_string();
    auto derived_bound = bind_script(catalog, *derived);
    ASSERT_TRUE(derived_bound) << derived_bound.error().to_string();
    const BoundSelectQuery& derived_select = select_statement(derived_bound->statements.front());
    const auto* const scalar = std::get_if<BoundScalarSubqueryExpression>(
        &projection_expression(derived_select.projection.front()).node
    );
    ASSERT_NE(scalar, nullptr);
    ASSERT_NE(scalar->query, nullptr);
    ASSERT_NE(scalar->query->select.from, nullptr);
    const auto* const derived_table =
        std::get_if<BoundDerivedTable>(&scalar->query->select.from->node);
    ASSERT_NE(derived_table, nullptr);
    ASSERT_NE(derived_table->query, nullptr);
    const BoundColumnReference* const derived_outer =
        column_reference(derived_table->query->select.projection.front());
    ASSERT_NE(derived_outer, nullptr);
    EXPECT_EQ(derived_outer->outer_query_levels, 1U);
}

TEST(BinderTest, EnforcesSubqueryColumnAndComparisonBoundaries) {
    FakeCatalogReadPort catalog{26U, {items_table(), metrics_table()}};

    auto scalar_many = parse_sql("SELECT (SELECT id, score FROM metrics) FROM items");
    ASSERT_TRUE(scalar_many) << scalar_many.error().to_string();
    const auto scalar_many_bound = bind_script(catalog, *scalar_many);
    ASSERT_FALSE(scalar_many_bound);
    EXPECT_EQ(scalar_many_bound.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_NE(scalar_many_bound.error().message().find("scalar subquery"), std::string::npos);

    auto in_many = parse_sql("SELECT id FROM items WHERE id IN (SELECT id, score FROM metrics)");
    ASSERT_TRUE(in_many) << in_many.error().to_string();
    const auto in_many_bound = bind_script(catalog, *in_many);
    ASSERT_FALSE(in_many_bound);
    EXPECT_EQ(in_many_bound.error().code(), base::ErrorCode::kUnsupported);
    EXPECT_NE(in_many_bound.error().message().find("IN subquery"), std::string::npos);

    auto exists_many =
        parse_sql("SELECT id FROM items WHERE EXISTS (SELECT id, score FROM metrics)");
    ASSERT_TRUE(exists_many) << exists_many.error().to_string();
    const auto exists_many_bound = bind_script(catalog, *exists_many);
    ASSERT_TRUE(exists_many_bound) << exists_many_bound.error().to_string();

    auto incompatible = parse_sql("SELECT id FROM items WHERE id IN (SELECT enabled FROM metrics)");
    ASSERT_TRUE(incompatible) << incompatible.error().to_string();
    const auto incompatible_bound = bind_script(catalog, *incompatible);
    ASSERT_FALSE(incompatible_bound);
    EXPECT_EQ(incompatible_bound.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(incompatible_bound.error().message().find("cannot coerce"), std::string::npos);
}

TEST(BinderTest, BindsDmlTargetsAssignmentCoercionAndQuerySources) {
    FakeCatalogReadPort catalog{27U, {items_table(), metrics_table()}};
    auto parsed = parse_sql(
        "INSERT INTO metrics (id, score, label) VALUES (1.5, 2, 'ok'); "
        "INSERT INTO metrics (id, score) SELECT id, id FROM items; "
        "UPDATE metrics SET id = score, label = 'x' WHERE enabled; "
        "DELETE FROM metrics WHERE id = 4"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 4U);

    const BoundInsertStatement& values = insert_statement(bound->statements.at(0U));
    EXPECT_EQ(values.target.oid, base::Oid{29U});
    ASSERT_EQ(values.target.columns.size(), 4U);
    EXPECT_EQ(values.target.columns.at(3U).name, "label");
    ASSERT_EQ(values.columns.size(), 3U);
    EXPECT_EQ(values.columns.at(0U), BoundColumnIndex{0U});
    EXPECT_EQ(values.columns.at(1U), BoundColumnIndex{1U});
    EXPECT_EQ(values.columns.at(2U), BoundColumnIndex{3U});
    const auto* const value_rows = std::get_if<BoundInsertValues>(&values.source);
    ASSERT_NE(value_rows, nullptr);
    ASSERT_EQ(value_rows->rows.size(), 1U);
    ASSERT_EQ(value_rows->rows.front().size(), 3U);
    EXPECT_EQ(value_rows->rows.front().at(0U)->data_type, types::kInt64Type);
    EXPECT_EQ(value_rows->rows.front().at(1U)->data_type, types::kFloat32Type);
    EXPECT_EQ(
        value_rows->rows.front().at(2U)->data_type,
        (types::DataType{types::kCharTypeId, types::TypeModifier{8}})
    );
    const BoundLiteralExpression* const rounded =
        literal_expression(*value_rows->rows.front().at(0U));
    ASSERT_NE(rounded, nullptr);
    ASSERT_NE(rounded->value.as_integer(), nullptr);
    EXPECT_EQ(*rounded->value.as_integer(), 2);

    const BoundInsertStatement& query_insert = insert_statement(bound->statements.at(1U));
    ASSERT_EQ(query_insert.columns.size(), 2U);
    const auto* const query_source = std::get_if<BoundInsertQuery>(&query_insert.source);
    ASSERT_NE(query_source, nullptr);
    ASSERT_NE(query_source->query, nullptr);
    ASSERT_EQ(query_source->query->select.projection.size(), 2U);
    EXPECT_EQ(
        query_source->query->select.projection.at(0U).expression->data_type,
        types::kInt64Type
    );
    EXPECT_EQ(
        query_source->query->select.projection.at(1U).expression->data_type,
        types::kFloat32Type
    );
    EXPECT_TRUE(
        std::holds_alternative<BoundCastExpression>(
            query_source->query->select.projection.at(1U).expression->node
        )
    );

    const BoundUpdateStatement& update = update_statement(bound->statements.at(2U));
    EXPECT_EQ(update.target.oid, base::Oid{29U});
    ASSERT_EQ(update.target.columns.size(), 4U);
    ASSERT_EQ(update.assignments.size(), 2U);
    EXPECT_EQ(update.assignments.at(0U).column_index, BoundColumnIndex{0U});
    ASSERT_NE(update.assignments.at(0U).value, nullptr);
    EXPECT_EQ(update.assignments.at(0U).value->data_type, types::kInt64Type);
    EXPECT_TRUE(std::holds_alternative<BoundCastExpression>(update.assignments.at(0U).value->node));
    EXPECT_EQ(update.assignments.at(1U).column_index, BoundColumnIndex{3U});
    ASSERT_NE(update.assignments.at(1U).value, nullptr);
    EXPECT_EQ(
        update.assignments.at(1U).value->data_type,
        (types::DataType{types::kCharTypeId, types::TypeModifier{8}})
    );
    ASSERT_NE(update.filter, nullptr);
    EXPECT_EQ(update.filter->data_type, types::kBoolType);
    EXPECT_TRUE(std::holds_alternative<BoundColumnReference>(update.filter->node));

    const BoundDeleteStatement& delete_bound = delete_statement(bound->statements.at(3U));
    EXPECT_EQ(delete_bound.target.oid, base::Oid{29U});
    ASSERT_EQ(delete_bound.target.columns.size(), 4U);
    ASSERT_NE(delete_bound.filter, nullptr);
    EXPECT_EQ(delete_bound.filter->data_type, types::kBoolType);
    EXPECT_TRUE(std::holds_alternative<BoundBinaryExpression>(delete_bound.filter->node));
}

TEST(BinderTest, RejectsInvalidDmlColumnsWidthsAndAssignmentTypes) {
    FakeCatalogReadPort catalog{28U, {items_table(), metrics_table()}};

    const auto assert_error = [&catalog](
                                  const std::string_view sql,
                                  const base::ErrorCode code,
                                  const std::string_view text
                              ) {
        auto parsed = parse_sql(sql);
        ASSERT_TRUE(parsed) << parsed.error().to_string();
        const auto bound = bind_script(catalog, *parsed);
        ASSERT_FALSE(bound);
        EXPECT_EQ(bound.error().code(), code);
        EXPECT_NE(bound.error().message().find(text), std::string::npos);
    };

    assert_error("INSERT INTO metrics (id, id) VALUES (1, 2)", base::ErrorCode::kBind, "duplicate");
    assert_error("INSERT INTO metrics VALUES (1)", base::ErrorCode::kUnsupported, "count mismatch");
    assert_error(
        "INSERT INTO metrics (id, score) SELECT id FROM items",
        base::ErrorCode::kUnsupported,
        "count mismatch"
    );
    assert_error("INSERT INTO metrics (missing) VALUES (1)", base::ErrorCode::kBind, "missing");
    assert_error("UPDATE metrics SET missing = 1", base::ErrorCode::kBind, "missing");
    assert_error("UPDATE metrics SET id = enabled", base::ErrorCode::kBind, "cannot coerce");
}

TEST(BinderTest, BindsDdlAndRejectsOutOfContractForms) {
    FakeCatalogReadPort catalog{29U, {metrics_table()}};
    auto parsed = parse_sql(
        "CREATE TABLE IF NOT EXISTS created (id INT, label CHAR(8), occurred TIMESTAMP(3)) "
        "WITH (fillfactor = 90); "
        "DROP TABLE IF EXISTS created; "
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_metrics_id_score ON metrics USING BTREE (id, "
        "score); "
        "DROP INDEX IF EXISTS idx_metrics_id_score"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 4U);

    const auto* const create_table =
        std::get_if<BoundCreateTableStatement>(&bound->statements.at(0U).node);
    ASSERT_NE(create_table, nullptr);
    EXPECT_EQ(create_table->table_name, "created");
    EXPECT_TRUE(create_table->if_not_exists);
    EXPECT_EQ(create_table->fillfactor, 90U);
    ASSERT_EQ(create_table->columns.size(), 3U);
    EXPECT_EQ(create_table->columns.at(0U).data_type, types::kInt64Type);
    EXPECT_EQ(
        create_table->columns.at(1U).data_type,
        (types::DataType{types::kCharTypeId, types::TypeModifier{8}})
    );
    EXPECT_EQ(
        create_table->columns.at(2U).data_type,
        (types::DataType{types::kTimestampTypeId, types::TypeModifier{3}})
    );

    const auto* const drop_table =
        std::get_if<BoundDropTableStatement>(&bound->statements.at(1U).node);
    ASSERT_NE(drop_table, nullptr);
    ASSERT_EQ(drop_table->table_names.size(), 1U);
    EXPECT_EQ(drop_table->table_names.front(), "created");
    EXPECT_TRUE(drop_table->if_exists);

    const auto* const create_index =
        std::get_if<BoundCreateIndexStatement>(&bound->statements.at(2U).node);
    ASSERT_NE(create_index, nullptr);
    EXPECT_EQ(create_index->name, "idx_metrics_id_score");
    EXPECT_EQ(create_index->table_oid, base::Oid{29U});
    EXPECT_TRUE(create_index->unique);
    EXPECT_TRUE(create_index->if_not_exists);
    ASSERT_EQ(create_index->columns.size(), 2U);
    EXPECT_EQ(create_index->columns.at(0U), BoundColumnIndex{0U});
    EXPECT_EQ(create_index->columns.at(1U), BoundColumnIndex{1U});

    const auto* const drop_index =
        std::get_if<BoundDropIndexStatement>(&bound->statements.at(3U).node);
    ASSERT_NE(drop_index, nullptr);
    ASSERT_EQ(drop_index->index_names.size(), 1U);
    EXPECT_EQ(drop_index->index_names.front(), "idx_metrics_id_score");
    EXPECT_TRUE(drop_index->if_exists);

    const auto assert_unsupported =
        [&catalog](const std::string_view sql, const std::string_view text) {
            auto source = parse_sql(sql);
            ASSERT_TRUE(source) << source.error().to_string();
            const auto rejected = bind_script(catalog, *source);
            ASSERT_FALSE(rejected);
            EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnsupported);
            EXPECT_NE(rejected.error().message().find(text), std::string::npos);
        };
    auto default_fillfactor = parse_sql("CREATE TABLE default_fillfactor (id INT)");
    ASSERT_TRUE(default_fillfactor) << default_fillfactor.error().to_string();
    auto default_fillfactor_bound = bind_script(catalog, *default_fillfactor);
    ASSERT_TRUE(default_fillfactor_bound) << default_fillfactor_bound.error().to_string();
    const auto* const default_create =
        std::get_if<BoundCreateTableStatement>(&default_fillfactor_bound->statements.front().node);
    ASSERT_NE(default_create, nullptr);
    EXPECT_EQ(default_create->fillfactor, 80U);
    assert_unsupported("CREATE TABLE invalid_fillfactor (id INT) WITH (fillfactor = 9)", "between");
    assert_unsupported(
        "CREATE TABLE noninteger_fillfactor (id INT) WITH (fillfactor = 'x')",
        "integer"
    );
    assert_unsupported("CREATE INDEX idx_hash ON metrics USING HASH (id)", "access method");
    assert_unsupported("DROP TABLE created, prior", "multiple names");
    assert_unsupported("DROP INDEX idx_metrics_id, idx_metrics_score", "multiple names");
    assert_unsupported("CREATE TABLE char_length_omitted (c CHAR)", "CHAR length");

    auto duplicate_column = parse_sql("CREATE TABLE duplicate_column (id INT, id FLOAT)");
    ASSERT_TRUE(duplicate_column) << duplicate_column.error().to_string();
    const auto duplicate_column_bound = bind_script(catalog, *duplicate_column);
    ASSERT_FALSE(duplicate_column_bound);
    EXPECT_EQ(duplicate_column_bound.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(duplicate_column_bound.error().message().find("duplicate"), std::string::npos);

    auto missing_index_column = parse_sql("CREATE INDEX idx_missing ON metrics (missing)");
    ASSERT_TRUE(missing_index_column) << missing_index_column.error().to_string();
    const auto missing_index_column_bound = bind_script(catalog, *missing_index_column);
    ASSERT_FALSE(missing_index_column_bound);
    EXPECT_EQ(missing_index_column_bound.error().code(), base::ErrorCode::kBind);
    EXPECT_NE(missing_index_column_bound.error().message().find("missing"), std::string::npos);
}

TEST(BinderTest, BindsUtilityAndTransactionStatements) {
    FakeCatalogReadPort catalog{30U, {metrics_table()}};
    auto parsed = parse_sql(
        "BEGIN; SET TRANSACTION ISOLATION LEVEL READ COMMITTED; "
        "EXPLAIN UPDATE metrics SET id = 1 WHERE enabled; SHOW TABLES; VACUUM metrics; ANALYZE; "
        "CHECKPOINT; COMMIT; ROLLBACK"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 9U);

    const auto* const begin =
        std::get_if<BoundTransactionStatement>(&bound->statements.at(0U).node);
    ASSERT_NE(begin, nullptr);
    EXPECT_EQ(begin->kind, TransactionKind::kBegin);
    const auto* const set =
        std::get_if<BoundSetTransactionStatement>(&bound->statements.at(1U).node);
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->isolation, IsolationLevel::kReadCommitted);

    const auto* const explain = std::get_if<BoundExplainStatement>(&bound->statements.at(2U).node);
    ASSERT_NE(explain, nullptr);
    ASSERT_NE(explain->statement, nullptr);
    EXPECT_TRUE(std::holds_alternative<BoundUpdateStatement>(explain->statement->node));
    EXPECT_TRUE(std::holds_alternative<BoundShowTablesStatement>(bound->statements.at(3U).node));
    const auto* const vacuum = std::get_if<BoundVacuumStatement>(&bound->statements.at(4U).node);
    ASSERT_NE(vacuum, nullptr);
    ASSERT_TRUE(vacuum->table_oid.has_value());
    EXPECT_EQ(*vacuum->table_oid, base::Oid{29U});
    const auto* const analyze = std::get_if<BoundAnalyzeStatement>(&bound->statements.at(5U).node);
    ASSERT_NE(analyze, nullptr);
    EXPECT_FALSE(analyze->table_oid.has_value());
    EXPECT_TRUE(std::holds_alternative<BoundCheckpointStatement>(bound->statements.at(6U).node));
    const auto* const commit =
        std::get_if<BoundTransactionStatement>(&bound->statements.at(7U).node);
    ASSERT_NE(commit, nullptr);
    EXPECT_EQ(commit->kind, TransactionKind::kCommit);
    const auto* const rollback =
        std::get_if<BoundTransactionStatement>(&bound->statements.at(8U).node);
    ASSERT_NE(rollback, nullptr);
    EXPECT_EQ(rollback->kind, TransactionKind::kRollback);
}

TEST(BinderTest, BindsCopyTextProjectionAndOptions) {
    FakeCatalogReadPort catalog{31U, {metrics_table()}};
    auto parsed = parse_sql(
        "COPY metrics (score, id) FROM STDIN WITH (FORMAT text, DELIMITER '|', NULL 'NULL')"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();
    auto bound = bind_script(catalog, *parsed);
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->statements.size(), 1U);
    const auto* const copy = std::get_if<BoundCopyFromStatement>(&bound->statements.front().node);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->target.oid, base::Oid{29U});
    ASSERT_EQ(copy->columns.size(), 2U);
    EXPECT_EQ(copy->columns.at(0U), BoundColumnIndex{1U});
    EXPECT_EQ(copy->columns.at(1U), BoundColumnIndex{0U});
    EXPECT_EQ(copy->delimiter, static_cast<std::uint8_t>('|'));
    EXPECT_EQ(copy->null_marker, "NULL");

    const auto unsupported_header = parse_sql("COPY metrics FROM STDIN WITH (HEADER)");
    ASSERT_FALSE(unsupported_header);
    EXPECT_EQ(unsupported_header.error().code(), base::ErrorCode::kUnsupported);
    const auto unsupported_direction = parse_sql("COPY metrics TO STDOUT");
    ASSERT_FALSE(unsupported_direction);
    EXPECT_EQ(unsupported_direction.error().code(), base::ErrorCode::kUnsupported);
}

TEST(BinderTest, BindsPreparedParametersAcrossQueriesDmlAndExplain) {
    FakeCatalogReadPort catalog{32U, {metrics_table()}};
    auto parsed = parse_sql(
        "SELECT $1 + id AS total FROM metrics WHERE score > $2 OR id = $1; "
        "INSERT INTO metrics (id, score) VALUES ($1, $2); "
        "EXPLAIN DELETE FROM metrics WHERE id = $1"
    );
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    const std::array<types::DataType, 2U> parameter_types{
        types::kInt64Type,
        types::kFloat32Type,
    };
    auto bound = bind_script_with_parameters(catalog, *parsed, parameter_types);
    ASSERT_TRUE(bound) << bound.error().to_string();
    ASSERT_EQ(bound->parameter_slots.size(), 2U);
    EXPECT_EQ(bound->parameter_slots.at(0U).slot, ParameterSlot{1U});
    EXPECT_EQ(bound->parameter_slots.at(0U).data_type, types::kInt64Type);
    EXPECT_EQ(bound->parameter_slots.at(0U).occurrences.size(), 4U);
    EXPECT_EQ(bound->parameter_slots.at(1U).slot, ParameterSlot{2U});
    EXPECT_EQ(bound->parameter_slots.at(1U).data_type, types::kFloat32Type);
    EXPECT_EQ(bound->parameter_slots.at(1U).occurrences.size(), 2U);

    const BoundSelectQuery& select = select_statement(bound->statements.front());
    const auto* const add =
        std::get_if<BoundBinaryExpression>(&projection_expression(select.projection.front()).node);
    ASSERT_NE(add, nullptr);
    ASSERT_NE(add->left, nullptr);
    const auto* const first_parameter = std::get_if<BoundParameter>(&add->left->node);
    ASSERT_NE(first_parameter, nullptr);
    EXPECT_EQ(first_parameter->slot, ParameterSlot{1U});
    EXPECT_EQ(first_parameter->data_type, types::kInt64Type);

    const BoundInsertStatement& insert = insert_statement(bound->statements.at(1U));
    const auto* const values = std::get_if<BoundInsertValues>(&insert.source);
    ASSERT_NE(values, nullptr);
    ASSERT_EQ(values->rows.size(), 1U);
    ASSERT_EQ(values->rows.front().size(), 2U);
    EXPECT_TRUE(std::holds_alternative<BoundParameter>(values->rows.front().at(0U)->node));
    EXPECT_TRUE(std::holds_alternative<BoundParameter>(values->rows.front().at(1U)->node));

    const auto* const explain = std::get_if<BoundExplainStatement>(&bound->statements.at(2U).node);
    ASSERT_NE(explain, nullptr);
    ASSERT_NE(explain->statement, nullptr);
    const auto* const deleted = std::get_if<BoundDeleteStatement>(&explain->statement->node);
    ASSERT_NE(deleted, nullptr);
    ASSERT_NE(deleted->filter, nullptr);
    const auto* const comparison = std::get_if<BoundBinaryExpression>(&deleted->filter->node);
    ASSERT_NE(comparison, nullptr);
    ASSERT_NE(comparison->right, nullptr);
    EXPECT_TRUE(std::holds_alternative<BoundParameter>(comparison->right->node));
}

TEST(BinderTest, RejectsInvalidPreparedParameterContracts) {
    FakeCatalogReadPort catalog{33U, {metrics_table()}};
    const auto assert_error = [&catalog](
                                  const std::string_view sql,
                                  const std::vector<types::DataType>& parameter_types,
                                  const std::string_view text
                              ) {
        auto parsed = parse_sql(sql);
        ASSERT_TRUE(parsed) << parsed.error().to_string();
        const auto bound = bind_script_with_parameters(catalog, *parsed, parameter_types);
        ASSERT_FALSE(bound);
        EXPECT_EQ(bound.error().code(), base::ErrorCode::kUnsupported);
        EXPECT_NE(bound.error().message().find(text), std::string::npos);
    };

    assert_error("SELECT $1", {}, "no declared type");
    assert_error("SELECT $2", {types::kInt64Type}, "no declared type");
    assert_error(
        "SELECT $1",
        {types::kInt64Type, types::kFloat32Type},
        "not every declared parameter"
    );
    assert_error("SELECT $1", {types::kUnknownType}, "invalid or unregistered");
}

TEST(BinderTest, PropagatesCatalogPortFailureWithoutBorrowingIt) {
    FailingCatalogReadPort catalog{};
    auto parsed = parse_sql("SELECT id FROM items");
    ASSERT_TRUE(parsed) << parsed.error().to_string();

    const auto bound = bind_script(catalog, *parsed);
    ASSERT_FALSE(bound);
    EXPECT_EQ(bound.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_NE(bound.error().message().find("captured catalog"), std::string::npos);
}

TEST(BinderTest, ConvertsCatalogPortExceptionsAtNamedAndDmlTableLookups) {
    ThrowingCatalogReadPort standard_catalog{CatalogExceptionKind::kStandard};
    auto select = parse_sql("SELECT id FROM items");
    ASSERT_TRUE(select) << select.error().to_string();

    const auto selected = bind_script(standard_catalog, *select);
    ASSERT_FALSE(selected);
    EXPECT_EQ(selected.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(
        selected.error().message().find("injected catalog lookup failure"),
        std::string::npos
    );
    ASSERT_EQ(selected.error().contexts().size(), 1U);
    EXPECT_EQ(selected.error().contexts().front().component, "sql");
    EXPECT_EQ(selected.error().contexts().front().operation, "lookup table");

    ThrowingCatalogReadPort allocation_catalog{CatalogExceptionKind::kAllocation};
    auto delete_statement = parse_sql("DELETE FROM items WHERE id = 1");
    ASSERT_TRUE(delete_statement) << delete_statement.error().to_string();

    const auto deleted = bind_script(allocation_catalog, *delete_statement);
    ASSERT_FALSE(deleted);
    EXPECT_EQ(deleted.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_EQ(
        deleted.error().message(),
        std::string_view{"catalog read port exhausted available memory"}
    );
    ASSERT_EQ(deleted.error().contexts().size(), 1U);
    EXPECT_EQ(deleted.error().contexts().front().component, "sql");
    EXPECT_EQ(deleted.error().contexts().front().operation, "lookup DML target table");
}

}  // namespace
}  // namespace db::sql
