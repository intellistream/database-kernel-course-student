#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "api_result_adapter.hpp"

namespace db::server {
namespace {

auto map_result(std::vector<api::ResultColumn> columns, std::vector<std::vector<types::Value>> rows)
    -> base::Result<protocol::ExecutionResult> {
    auto schema = api::ResultSchema::create(std::move(columns));
    if (!schema) {
        return base::fail(std::move(schema).error());
    }
    auto result = api::ResultSet::create(std::move(schema).value(), std::move(rows));
    if (!result) {
        return base::fail(std::move(result).error());
    }
    api::DispatchResult dispatch;
    dispatch.statement_count = 1U;
    dispatch.result_sets.push_back(std::move(result).value());
    return internal::map_api_dispatch_result({}, std::move(dispatch));
}

TEST(ApiResultAdapterTest, OwnsMappedScalarsTextTimestampsAndTypedNullsAfterSourceDestruction) {
    const std::string text(256U, 'x');
    auto result = map_result(
        {
            {.name = "value", .data_type = types::kInt64Type},
            {.name = "value", .data_type = types::kFloat32Type},
            {.name = "text", .data_type = types::kCharType},
            {.name = "time", .data_type = types::kTimestampType},
        },
        {
            {types::Value::integer(std::numeric_limits<std::int32_t>::min()),
             types::Value::floating(1.5F),
             types::Value::character(text),
             types::Value::timestamp(types::TimestampValue{0})},
            {types::Value::null(types::kInt64Type),
             types::Value::null(types::kFloat32Type),
             types::Value::null(types::kCharType),
             types::Value::null(types::kTimestampType)},
        }
    );
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->columns().size(), 4U);
    EXPECT_EQ(result->columns()[0].name, "value");
    EXPECT_EQ(result->columns()[1].name, "value");
    EXPECT_EQ(result->columns()[3].data_type, protocol::WireDataType::kChar);
    ASSERT_EQ(result->rows().size(), 2U);
    EXPECT_EQ(result->rows()[0][0].integer(), std::numeric_limits<std::int32_t>::min());
    EXPECT_EQ(result->rows()[0][1].floating(), 1.5F);
    EXPECT_EQ(result->rows()[0][2].text(), text);
    EXPECT_EQ(result->rows()[0][3].text(), "2000-01-01 00:00:00");
    for (const auto& cell : result->rows()[1]) {
        EXPECT_TRUE(cell.is_null());
    }
}

TEST(ApiResultAdapterTest, PreservesEmptyQueryMetadataAndRejectsUnrepresentableResults) {
    auto empty = map_result({{.name = "id", .data_type = types::kInt64Type}}, {});
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->kind(), protocol::ExecutionKind::kQuery);
    EXPECT_EQ(empty->columns().size(), 1U);
    EXPECT_TRUE(empty->rows().empty());

    auto wide_integer = map_result(
        {{.name = "id", .data_type = types::kInt64Type}},
        {{types::Value::integer(
            static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1
        )}}
    );
    ASSERT_FALSE(wide_integer);
    EXPECT_EQ(wide_integer.error().code(), base::ErrorCode::kOutOfRange);

    auto invalid_text = map_result(
        {{.name = "text", .data_type = types::kCharType}},
        {{types::Value::character(std::string(1U, '\xFF'))}}
    );
    ASSERT_FALSE(invalid_text);
    EXPECT_EQ(invalid_text.error().code(), base::ErrorCode::kOutOfRange);

    auto invalid_name =
        map_result({{.name = std::string(1U, '\xFF'), .data_type = types::kCharType}}, {});
    ASSERT_FALSE(invalid_name);
    EXPECT_EQ(invalid_name.error().code(), base::ErrorCode::kExecution);
}

}  // namespace
}  // namespace db::server
