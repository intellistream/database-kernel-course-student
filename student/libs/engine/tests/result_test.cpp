#include <array>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/engine/result.hpp"
#include "db/types/value.hpp"

namespace db::engine {
namespace {

TEST(ResultSchemaTest, RetainsDuplicateProjectionNamesAndFindsTheFirstPosition) {
    auto schema = ResultSchema::create({
        ResultColumn{.name = "id", .data_type = types::kInt64Type},
        ResultColumn{.name = "id", .data_type = types::kInt64Type},
        ResultColumn{.name = "label", .data_type = types::kCharType},
    });
    ASSERT_TRUE(schema) << schema.error().to_string();
    EXPECT_EQ(schema->size(), 3U);
    EXPECT_FALSE(schema->empty());
    ASSERT_TRUE(schema->position("id"));
    EXPECT_EQ(schema->position("id").value(), 0U);
    ASSERT_TRUE(schema->at(2U));
    EXPECT_EQ(schema->at(2U)->get().name, "label");

    auto missing = schema->position("missing");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), base::ErrorCode::kNotFound);
    auto out_of_range = schema->at(3U);
    ASSERT_FALSE(out_of_range);
    EXPECT_EQ(out_of_range.error().code(), base::ErrorCode::kOutOfRange);
}

TEST(ResultSchemaTest, RejectsEmptyNamesUnknownTypesAndInvalidModifiers) {
    auto empty_name = ResultSchema::create({
        ResultColumn{.name = "", .data_type = types::kInt64Type},
    });
    ASSERT_FALSE(empty_name);
    EXPECT_EQ(empty_name.error().code(), base::ErrorCode::kInvalidArgument);

    auto unknown = ResultSchema::create({
        ResultColumn{.name = "value", .data_type = types::kUnknownType},
    });
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code(), base::ErrorCode::kInvalidArgument);

    auto invalid_modifier = ResultSchema::create({
        ResultColumn{
            .name = "value",
            .data_type = types::DataType{types::kCharTypeId, types::TypeModifier{0}},
        },
    });
    ASSERT_FALSE(invalid_modifier);
    EXPECT_EQ(invalid_modifier.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(ResultRowTest, OwnsTypedValuesAndUsesStructuredAccessErrors) {
    auto schema = ResultSchema::create({
        ResultColumn{.name = "id", .data_type = types::kInt64Type},
        ResultColumn{.name = "label", .data_type = types::kCharType},
    });
    ASSERT_TRUE(schema) << schema.error().to_string();
    auto row = ResultRow::create(
        std::move(schema).value(),
        {types::Value::integer(7), types::Value::character("hello")}
    );
    ASSERT_TRUE(row) << row.error().to_string();
    EXPECT_TRUE(row->valid());
    EXPECT_EQ(row->size(), 2U);
    ASSERT_TRUE(row->at(0U));
    EXPECT_EQ(row->at(0U)->get(), types::Value::integer(7));
    ASSERT_TRUE(row->at("label"));
    EXPECT_EQ(row->at("label")->get(), types::Value::character("hello"));

    auto missing = row->at("missing");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), base::ErrorCode::kNotFound);
    auto out_of_range = row->at(2U);
    ASSERT_FALSE(out_of_range);
    EXPECT_EQ(out_of_range.error().code(), base::ErrorCode::kOutOfRange);
}

TEST(ResultSetTest, OwnsRowsSharesSchemaAndRejectsShapeOrTypeMismatch) {
    auto schema = ResultSchema::create({
        ResultColumn{.name = "id", .data_type = types::kInt64Type},
        ResultColumn{.name = "label", .data_type = types::kCharType},
    });
    ASSERT_TRUE(schema) << schema.error().to_string();
    auto result = ResultSet::create(
        std::move(schema).value(),
        std::vector<std::vector<types::Value>>{
            {types::Value::integer(1), types::Value::character("one")},
            {types::Value::integer(2), types::Value::character("two")},
        }
    );
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_TRUE(result->valid());
    EXPECT_EQ(result->size(), 2U);
    EXPECT_EQ(result->schema().position("id").value(), 0U);
    ASSERT_TRUE(result->at(1U));
    EXPECT_EQ(result->at(1U)->get().at("label")->get(), types::Value::character("two"));

    auto mismatch_schema = ResultSchema::create({
        ResultColumn{.name = "id", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(mismatch_schema);
    auto mismatch = ResultSet::create(
        std::move(mismatch_schema).value(),
        std::vector<std::vector<types::Value>>{{types::Value::character("wrong")}}
    );
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error().code(), base::ErrorCode::kInvalidArgument);

    auto empty_schema = ResultSchema::create({});
    ASSERT_TRUE(empty_schema);
    auto empty = ResultSet::create(std::move(empty_schema).value(), {});
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->empty());
    EXPECT_TRUE(empty->schema().empty());
    auto empty_data = std::move(empty).value().into_data();
    EXPECT_TRUE(empty_data.columns.empty());
    EXPECT_TRUE(empty_data.rows.empty());
}

TEST(ResultSetTest, TransfersCanonicalRowsWithoutAValidationWrapper) {
    auto schema = ResultSchema::create({
        ResultColumn{.name = "id", .data_type = types::kInt64Type},
        ResultColumn{.name = "label", .data_type = types::kCharType},
    });
    ASSERT_TRUE(schema) << schema.error().to_string();
    auto created = ResultSet::create(
        std::move(schema).value(),
        std::vector<std::vector<types::Value>>{{
            types::Value::integer(7),
            types::Value::character("seven"),
        }}
    );
    ASSERT_TRUE(created) << created.error().to_string();
    ResultSet source = std::move(created).value();

    auto data = std::move(source).into_data();
    // Inspecting the documented inert source verifies the ownership handoff.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_FALSE(source.valid());
    ASSERT_EQ(data.columns.size(), 2U);
    EXPECT_EQ(data.columns[0U].name, "id");
    ASSERT_EQ(data.rows.size(), 1U);
    EXPECT_EQ(data.rows[0U][0U], types::Value::integer(7));
    EXPECT_EQ(data.rows[0U][1U], types::Value::character("seven"));
}

}  // namespace
}  // namespace db::engine
