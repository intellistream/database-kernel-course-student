#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/catalog/analyze.hpp"
#include "db/catalog/schema.hpp"
#include "db/catalog/statistics.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

namespace db::catalog {
namespace {

constexpr RowSizeLimit kRowLimit{.maximum_encoded_size = 4'036U};

auto statistics_schema() -> Schema {
    auto schema = Schema::create(
        std::vector<Column>{
            Column{.name = "id", .data_type = types::kInt64Type},
            Column{
                .name = "label",
                .data_type = types::DataType{types::kCharTypeId, types::TypeModifier{12}},
            },
        },
        kRowLimit
    );
    EXPECT_TRUE(schema) << schema.error().to_string();
    return std::move(schema).value();
}

TEST(AnalyzeAccumulatorTest, UsesReferenceDeterministicReservoirAndExactRowCounters) {
    EXPECT_EQ(kDefaultAnalyzeSampleRows, 3'000U);
    auto accumulator = AnalyzeAccumulator::create(base::Oid{7U}, statistics_schema(), 3U);
    ASSERT_TRUE(accumulator);
    const auto char_type = statistics_schema().columns()[1].data_type;
    for (std::int64_t value = 0; value < 10; ++value) {
        const std::vector<types::Value> row{
            types::Value::integer(value),
            value % 2 == 0 ? types::Value::character("even", char_type.modifier())
                           : types::Value::null(char_type),
        };
        ASSERT_TRUE(accumulator->observe(row, static_cast<std::size_t>(10 + value)));
    }
    EXPECT_EQ(accumulator->live_rows(), 10U);
    EXPECT_EQ(accumulator->sample_rows(), 3U);

    const auto result = accumulator->finish(
        AnalyzeCompletion{
            .relation_pages = 4U,
            .known_dead_rows = 2U,
            .analyzed_xid = base::Xid{19U},
        }
    );
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_EQ(result->live_rows, 10U);
    EXPECT_EQ(result->dead_rows, 2U);
    EXPECT_EQ(result->average_row_width, 14U);
    EXPECT_EQ(result->sample_rows, 3U);
    ASSERT_EQ(result->columns.size(), 2U);
    EXPECT_EQ(result->columns[0].sample_distinct, 3U);
    EXPECT_EQ(result->columns[0].minimum, types::Value::integer(0));
    EXPECT_EQ(result->columns[0].maximum, types::Value::integer(8));
    EXPECT_EQ(result->columns[0].average_width, 8U);
    EXPECT_DOUBLE_EQ(result->columns[1].null_fraction, 0.0);
    EXPECT_EQ(result->columns[1].sample_distinct, 1U);
    EXPECT_EQ(result->columns[1].average_width, 4U);
}

TEST(AnalyzeAccumulatorTest, CalculatesTypedNullAndEmptySampleStatistics) {
    const auto schema = statistics_schema();
    const auto char_type = schema.columns()[1].data_type;
    auto accumulator = AnalyzeAccumulator::create(base::Oid{3U}, schema, 10U);
    ASSERT_TRUE(accumulator);
    ASSERT_TRUE(accumulator->observe(
        std::vector<types::Value>{
            types::Value::integer(3),
            types::Value::null(char_type),
        },
        16U
    ));
    ASSERT_TRUE(accumulator->observe(
        std::vector<types::Value>{
            types::Value::integer(1),
            types::Value::character("abc", char_type.modifier()),
        },
        20U
    ));
    ASSERT_TRUE(accumulator->observe(
        std::vector<types::Value>{
            types::Value::integer(3),
            types::Value::null(char_type),
        },
        16U
    ));
    const auto result = accumulator->finish(
        AnalyzeCompletion{
            .relation_pages = 2U,
            .known_dead_rows = 1U,
            .analyzed_xid = base::Xid{5U},
        }
    );
    ASSERT_TRUE(result);
    EXPECT_EQ(result->columns[0].sample_distinct, 2U);
    EXPECT_EQ(result->columns[0].minimum, types::Value::integer(1));
    EXPECT_EQ(result->columns[0].maximum, types::Value::integer(3));
    EXPECT_DOUBLE_EQ(result->columns[1].null_fraction, 2.0 / 3.0);
    EXPECT_EQ(result->columns[1].average_width, 3U);

    auto empty = AnalyzeAccumulator::create(base::Oid{3U}, schema, 0U);
    ASSERT_TRUE(empty);
    const auto empty_result = empty->finish(
        AnalyzeCompletion{
            .analyzed_xid = base::Xid{6U},
        }
    );
    ASSERT_TRUE(empty_result);
    EXPECT_EQ(empty_result->sample_rows, 0U);
    for (const auto& column : empty_result->columns) {
        EXPECT_DOUBLE_EQ(column.null_fraction, 0.0);
        EXPECT_EQ(column.average_width, 0U);
        EXPECT_EQ(column.sample_distinct, 0U);
        EXPECT_FALSE(column.minimum);
        EXPECT_FALSE(column.maximum);
    }
}

TEST(AnalyzeAccumulatorTest, RejectsInvalidInputsWithoutChangingCounters) {
    const auto schema = statistics_schema();
    auto accumulator = AnalyzeAccumulator::create(base::Oid{9U}, schema, 2U);
    ASSERT_TRUE(accumulator);
    EXPECT_EQ(
        accumulator->observe(std::vector<types::Value>{types::Value::integer(1)}, 8U)
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        accumulator
            ->observe(
                std::vector<types::Value>{
                    types::Value::boolean(true),
                    types::Value::null(schema.columns()[1].data_type),
                },
                8U
            )
            .error()
            .code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(accumulator->live_rows(), 0U);
    EXPECT_EQ(
        accumulator->finish(AnalyzeCompletion{}).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        AnalyzeAccumulator::create(base::kInvalidOid, schema, 2U).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    EXPECT_EQ(
        AnalyzeAccumulator::create(base::Oid{1U}, schema, kMaximumAnalyzeSampleRows + 1U)
            .error()
            .code(),
        base::ErrorCode::kResourceExhausted
    );
}

TEST(TableStatisticsTest, RejectsStaleOrImpossibleStatistics) {
    const auto schema = statistics_schema();
    TableStatistics statistics{
        .table_oid = base::Oid{1U},
        .live_rows = 2U,
        .average_row_width = 16U,
        .sample_rows = 2U,
        .analyzed_xid = base::Xid{4U},
        .columns = {
            ColumnStatistics{
                .data_type = types::kInt64Type,
                .average_width = 8U,
                .sample_distinct = 2U,
                .minimum = types::Value::integer(1),
                .maximum = types::Value::integer(2),
            },
            ColumnStatistics{
                .data_type = schema.columns()[1].data_type,
                .null_fraction = 1.0,
            },
        },
    };
    EXPECT_TRUE(validate_table_statistics(statistics, schema));

    auto invalid = statistics;
    invalid.columns[0].data_type = types::kBoolType;
    EXPECT_EQ(
        validate_table_statistics(invalid, schema).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    invalid = statistics;
    invalid.columns[0].null_fraction = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(
        validate_table_statistics(invalid, schema).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    invalid = statistics;
    invalid.columns[0].sample_distinct = 3U;
    EXPECT_EQ(
        validate_table_statistics(invalid, schema).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    invalid = statistics;
    invalid.columns[0].null_fraction = 0.25;
    EXPECT_EQ(
        validate_table_statistics(invalid, schema).error().code(),
        base::ErrorCode::kInvalidArgument
    );
    invalid = statistics;
    invalid.columns[0].minimum = types::Value::integer(5);
    EXPECT_EQ(
        validate_table_statistics(invalid, schema).error().code(),
        base::ErrorCode::kInvalidArgument
    );
}

}  // namespace
}  // namespace db::catalog
