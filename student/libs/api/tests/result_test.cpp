#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/api/result.hpp"

#include "allocation_failure.hpp"

namespace db::api {
namespace {

TEST(ApiResultSchemaTest, PreservesDuplicateNamesAndReportsStructuredLookupErrors) {
    auto schema = ResultSchema::create({
        ResultColumn{.name = "id", .data_type = types::kInt64Type},
        ResultColumn{.name = "id", .data_type = types::kInt64Type},
        ResultColumn{.name = "label", .data_type = types::kCharType},
    });
    ASSERT_TRUE(schema) << schema.error().to_string();
    EXPECT_EQ(schema->size(), 3U);
    ASSERT_TRUE(schema->position("id"));
    EXPECT_EQ(schema->position("id").value(), 0U);

    auto missing = schema->position("missing");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), base::ErrorCode::kNotFound);
    auto out_of_range = schema->at(3U);
    ASSERT_FALSE(out_of_range);
    EXPECT_EQ(out_of_range.error().code(), base::ErrorCode::kOutOfRange);
}

TEST(ApiResultSchemaTest, RejectsInvalidPublicMetadata) {
    auto empty_name = ResultSchema::create({
        ResultColumn{.name = "", .data_type = types::kInt64Type},
    });
    ASSERT_FALSE(empty_name);
    EXPECT_EQ(empty_name.error().code(), base::ErrorCode::kInvalidArgument);

    auto unknown_type = ResultSchema::create({
        ResultColumn{.name = "value", .data_type = types::kUnknownType},
    });
    ASSERT_FALSE(unknown_type);
    EXPECT_EQ(unknown_type.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(ApiResultSetTest, OwnsTypedRowsAndRejectsShapeOrTypeMismatch) {
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
    ASSERT_TRUE(result->at(1U));
    ASSERT_TRUE(result->at(1U)->get().at("label"));
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
}

TEST(ApiResultErrorTest, MapsBaseCodesToStableDatabaseCategories) {
    EXPECT_EQ(database_error_kind(base::ErrorCode::kParse), DatabaseErrorKind::kParse);
    EXPECT_EQ(
        database_error_kind(base::ErrorCode::kOutOfRange),
        DatabaseErrorKind::kInvalidArgument
    );
    EXPECT_EQ(database_error_kind(base::ErrorCode::kCorruption), DatabaseErrorKind::kInternal);
    EXPECT_EQ(
        database_error_kind_name(DatabaseErrorKind::kSerializationFailure),
        std::string_view{"serialization_failure"}
    );
}

// These tests deliberately exercise the documented post-consumption/move state.
// NOLINTBEGIN(bugprone-use-after-move)
TEST(ApiResultSetTest, ConsumesRowStorageAndPreservesIndependentCopies) {
    auto schema = ResultSchema::create({
        ResultColumn{.name = "value", .data_type = types::kCharType},
        ResultColumn{.name = "value", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(schema);
    const std::string text(256U, 'x');
    auto result = ResultSet::create(
        schema.value(),
        {
            {types::Value::character(text), types::Value::integer(42)},
            {types::Value::null(types::kCharType), types::Value::null(types::kInt64Type)},
        }
    );
    ASSERT_TRUE(result);
    const auto copy = result.value();
    const auto row_copy = result->rows().front();
    const auto* row_storage = result->rows().front().values().data();
    const auto* null_storage = result->rows().back().values().data();
    const auto* schema_storage = &result->schema();
    const auto* text_storage = result->rows().front().values().front().as_string()->data();

    auto data = std::move(result).value().into_data();
    ASSERT_TRUE(data) << data.error().to_string();
    ASSERT_NE(data->schema, nullptr);
    EXPECT_EQ(*data->schema, schema.value());
    EXPECT_EQ(data->schema.get(), schema_storage);
    ASSERT_EQ(data->rows.size(), 2U);
    EXPECT_EQ(data->rows[0].data(), row_storage);
    EXPECT_EQ(data->rows[1].data(), null_storage);
    EXPECT_EQ(data->rows[0][0].as_string()->data(), text_storage);
    EXPECT_EQ(data->rows[1][0], types::Value::null(types::kCharType));
    EXPECT_EQ(data->rows[1][1], types::Value::null(types::kInt64Type));
    EXPECT_FALSE(result->valid());
    EXPECT_TRUE(result->schema().empty());
    EXPECT_TRUE(result->rows().empty());
    EXPECT_EQ(result->size(), 0U);
    EXPECT_TRUE(result->empty());
    auto missing = result->at(0U);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code(), base::ErrorCode::kOutOfRange);
    auto consumed_twice = std::move(result).value().into_data();
    ASSERT_FALSE(consumed_twice);
    EXPECT_EQ(consumed_twice.error().code(), base::ErrorCode::kInvalidArgument);

    data->rows[0][0] = types::Value::character("replacement");
    EXPECT_TRUE(copy.valid());
    EXPECT_TRUE(row_copy.valid());
    EXPECT_EQ(copy.rows()[0].values()[0], types::Value::character(text));
    EXPECT_EQ(row_copy.values()[0], types::Value::character(text));
    result.value() = copy;
    EXPECT_TRUE(result->valid());
    EXPECT_EQ(result.value(), copy);
}

TEST(ApiResultSetTest, ConsumesEmptyResultsAndRejectsMovedFromResults) {
    auto schema = ResultSchema::create({
        ResultColumn{.name = "value", .data_type = types::kInt64Type},
    });
    ASSERT_TRUE(schema);
    auto empty = ResultSet::create(schema.value(), {});
    ASSERT_TRUE(empty);
    auto owner = std::move(empty).value();
    auto moved_from = std::move(empty).value().into_data();
    ASSERT_FALSE(moved_from);
    EXPECT_EQ(moved_from.error().code(), base::ErrorCode::kInvalidArgument);

    auto data = std::move(owner).into_data();
    ASSERT_TRUE(data);
    ASSERT_NE(data->schema, nullptr);
    EXPECT_EQ(*data->schema, schema.value());
    EXPECT_TRUE(data->rows.empty());
    EXPECT_FALSE(owner.valid());
    EXPECT_TRUE(owner.schema().empty());

    auto no_columns = ResultSet::create(ResultSchema{}, {{}, {}});
    ASSERT_TRUE(no_columns);
    auto zero_width = std::move(no_columns).value().into_data();
    ASSERT_TRUE(zero_width);
    ASSERT_NE(zero_width->schema, nullptr);
    EXPECT_TRUE(zero_width->schema->empty());
    ASSERT_EQ(zero_width->rows.size(), 2U);
    EXPECT_TRUE(zero_width->rows[0].empty());
    EXPECT_TRUE(zero_width->rows[1].empty());
}

TEST(ApiResultSetTest, AllocationFailuresLeaveAllSourceStorageIntact) {
    auto schema = ResultSchema::create({
        ResultColumn{.name = std::string(128U, 'n'), .data_type = types::kCharType},
    });
    ASSERT_TRUE(schema);
    auto result = ResultSet::create(
        schema.value(),
        {{types::Value::character(std::string(256U, 'x'))}, {types::Value::null(types::kCharType)}}
    );
    ASSERT_TRUE(result);
    const auto copy = result.value();
    const auto* schema_storage = result->schema().columns().data();
    const auto* row_storage = result->rows()[0].values().data();
    const auto* text_storage = result->rows().front().values().front().as_string()->data();
    bool succeeded = false;
    std::size_t failures = 0U;
    for (std::size_t allocation = 0U; allocation < 64U; ++allocation) {
        tests::fail_allocation_after(allocation);
        auto data = std::move(result).value().into_data();
        tests::stop_failing_allocations();
        if (data) {
            EXPECT_EQ(data->rows[0].data(), row_storage);
            EXPECT_EQ(data->rows[0][0].as_string()->data(), text_storage);
            EXPECT_FALSE(result->valid());
            succeeded = true;
            break;
        }
        ++failures;
        EXPECT_EQ(data.error().code(), base::ErrorCode::kResourceExhausted);
        EXPECT_TRUE(result->valid());
        EXPECT_EQ(result.value(), copy);
        EXPECT_EQ(result->schema().columns().data(), schema_storage);
        EXPECT_EQ(result->rows()[0].values().data(), row_storage);
        EXPECT_EQ(result->rows()[0].values()[0].as_string()->data(), text_storage);
    }
    EXPECT_TRUE(succeeded);
    EXPECT_GT(failures, 0U);
}

// NOLINTEND(bugprone-use-after-move)

}  // namespace
}  // namespace db::api
