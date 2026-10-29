#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/protocol/response_encoder.hpp"
#include "db/types/type_io.hpp"
#include "db/types/value.hpp"

#include "rmdb_v3_fixture_corpus.hpp"

namespace db::protocol {
namespace {

class CollectingSink final : public ResponseSink {
public:
    auto write_encoded_frame(const base::Bytes frame) -> base::Status override {
        frames.emplace_back(frame.begin(), frame.end());
        return status;
    }

    base::Status status{};
    std::vector<std::vector<std::byte>> frames;
};

enum class ThrowingSinkMode : std::uint8_t {
    kAllocation,
    kStandard,
};

class ThrowingSink final : public ResponseSink {
public:
    explicit ThrowingSink(const ThrowingSinkMode mode) noexcept
        : mode_{mode} {}

    auto write_encoded_frame(const base::Bytes frame) -> base::Status override {
        static_cast<void>(frame);
        if (mode_ == ThrowingSinkMode::kAllocation) {
            throw std::bad_alloc{};
        }
        throw std::runtime_error{"configured response-sink failure"};
    }

private:
    ThrowingSinkMode mode_;
};

template <std::size_t Size>
void expect_frame_eq(
    const std::vector<std::byte>& actual,
    const std::array<std::byte, Size>& expected
) {
    EXPECT_EQ(actual.size(), expected.size());
    EXPECT_TRUE(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()));
}

auto two_column_query() -> ExecutionResult {
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "total", .data_type = WireDataType::kInt32},
        ColumnMetadata{.name = "note", .data_type = WireDataType::kChar},
    };
    auto result = ExecutionResult::query(
        columns,
        {{types::Value::integer(42), types::Value::character("hi")}}
    );
    if (!result) {
        ADD_FAILURE() << result.error().to_string();
        std::terminate();
    }
    return std::move(result).value();
}

TEST(ResponseEncoderTest, EncodesThePinnedStreamQuerySequenceByteForByte) {
    ResponseEncoder encoder;
    CollectingSink sink;
    const RequestResponse response{StreamResponse{.execution = two_column_query()}};

    ASSERT_TRUE(encoder.emit(response, sink));
    ASSERT_EQ(sink.frames.size(), 3U);
    expect_frame_eq(sink.frames[0U], test::rmdb_v3_fixture::kMetaTwoColumns);
    expect_frame_eq(sink.frames[1U], test::rmdb_v3_fixture::kRowTwoValues);
    expect_frame_eq(sink.frames[2U], test::rmdb_v3_fixture::kResultEndOne);
}

TEST(ResponseEncoderTest, ConvertsEscapedSinkExceptionsWithTheCallingOperation) {
    ResponseEncoder encoder;
    const RequestResponse command{StreamResponse{.execution = ExecutionResult{}}};
    ThrowingSink allocation_sink{ThrowingSinkMode::kAllocation};

    const auto allocation = encoder.emit(command, allocation_sink);
    ASSERT_FALSE(allocation);
    EXPECT_EQ(allocation.error().code(), base::ErrorCode::kResourceExhausted);
    ASSERT_EQ(allocation.error().contexts().size(), 1U);
    EXPECT_EQ(allocation.error().contexts().front().component, "protocol.response_encoder");
    EXPECT_EQ(allocation.error().contexts().front().operation, "emit response");

    ThrowingSink standard_sink{ThrowingSinkMode::kStandard};
    const base::Error diagnostic{base::ErrorCode::kExecution, "configured diagnostic"};
    const auto standard =
        encoder.emit_terminal_error(TerminalResponseKind::kError, diagnostic, standard_sink);
    ASSERT_FALSE(standard);
    EXPECT_EQ(standard.error().code(), base::ErrorCode::kInternal);
    ASSERT_EQ(standard.error().contexts().size(), 1U);
    EXPECT_EQ(standard.error().contexts().front().component, "protocol.response_encoder");
    EXPECT_EQ(standard.error().contexts().front().operation, "emit terminal response");
}

TEST(ResponseEncoderTest, EncodesCommandAndPrepareResponsesByteForByte) {
    ResponseEncoder encoder;
    CollectingSink sink;
    const RequestResponse command{StreamResponse{.execution = ExecutionResult{}}};

    ASSERT_TRUE(encoder.emit(command, sink));
    ASSERT_EQ(sink.frames.size(), 1U);
    expect_frame_eq(sink.frames.front(), test::rmdb_v3_fixture::kCommandOk);

    const RequestResponse prepared{PrepareResponse{
        .statements = {
            PreparedStatementMetadata{
                .statement_id = 7U,
                .columns = {
                    ColumnMetadata{.name = "total", .data_type = WireDataType::kInt32},
                    ColumnMetadata{.name = "note", .data_type = WireDataType::kChar},
                },
            },
        },
    }};
    ASSERT_TRUE(encoder.emit(prepared, sink));
    ASSERT_EQ(sink.frames.size(), 2U);
    expect_frame_eq(sink.frames.back(), test::rmdb_v3_fixture::kPrepareOkTwoColumns);
}

TEST(ResponseEncoderTest, EncodesPinnedBatchSuccessAndFailureModelsByteForByte) {
    ResponseEncoder encoder;
    CollectingSink sink;
    const RequestResponse success{BatchSuccessResponse{
        .executed_operation_count = 1U,
        .query_results = {
            BatchQueryResult{
                .operation_index = 0U,
                .execution = two_column_query(),
            },
        },
    }};

    ASSERT_TRUE(encoder.emit(success, sink));
    ASSERT_EQ(sink.frames.size(), 1U);
    expect_frame_eq(sink.frames.back(), test::rmdb_v3_fixture::kBatchResultSuccess);

    const RequestResponse failure{BatchFailureResponse{
        .executed_operation_count = 0U,
        .failed_operation = 0U,
        .retryable = true,
        .error = base::Error{base::ErrorCode::kSerializationFailure, "ssi"},
    }};
    ASSERT_TRUE(encoder.emit(failure, sink));
    ASSERT_EQ(sink.frames.size(), 2U);
    expect_frame_eq(sink.frames.back(), test::rmdb_v3_fixture::kBatchResultTransactionAbort);
}

TEST(ResponseEncoderTest, EncodesPinnedTerminalResponsesByteForByte) {
    ResponseEncoder encoder;
    CollectingSink sink;
    const RequestResponse error{TerminalErrorResponse{
        .kind = TerminalResponseKind::kError,
        .error = base::Error{base::ErrorCode::kParse, "bad SQL"},
    }};
    const RequestResponse abort{TerminalErrorResponse{
        .kind = TerminalResponseKind::kTransactionAbort,
        .error = base::Error{base::ErrorCode::kSerializationFailure, "retry"},
    }};

    ASSERT_TRUE(encoder.emit(error, sink));
    ASSERT_TRUE(encoder.emit(abort, sink));
    ASSERT_EQ(sink.frames.size(), 2U);
    expect_frame_eq(sink.frames[0U], test::rmdb_v3_fixture::kErrorBadSql);
    expect_frame_eq(sink.frames[1U], test::rmdb_v3_fixture::kTransactionAbortRetry);
}

TEST(ResponseEncoderTest, PreservesFloatBitsAndNullCells) {
    ResponseEncoder encoder;
    CollectingSink sink;
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "value", .data_type = WireDataType::kFloat32},
        ColumnMetadata{.name = "note", .data_type = WireDataType::kChar},
    };
    auto execution = ExecutionResult::query(
        columns,
        {{
            types::Value::floating(std::bit_cast<float>(std::uint32_t{0x80000000U})),
            types::Value::null(types::kCharType),
        }}
    );
    ASSERT_TRUE(execution) << execution.error().to_string();
    const RequestResponse response{StreamResponse{.execution = std::move(execution).value()}};

    ASSERT_TRUE(encoder.emit(response, sink));
    ASSERT_EQ(sink.frames.size(), 3U);
    ASSERT_EQ(sink.frames[1U].size(), kRmdbFrameHeaderSize + 6U);
    EXPECT_EQ(sink.frames[1U][kRmdbFrameHeaderSize], std::byte{1U});
    EXPECT_EQ(sink.frames[1U][kRmdbFrameHeaderSize + 1U], std::byte{0x80U});
    EXPECT_EQ(sink.frames[1U][kRmdbFrameHeaderSize + 4U], std::byte{0U});
    EXPECT_EQ(sink.frames[1U][kRmdbFrameHeaderSize + 5U], std::byte{0U});
}

TEST(ResponseEncoderTest, MapsTimestampResultsToThePublishedCharTextEncoding) {
    ResponseEncoder encoder;
    CollectingSink sink;
    const types::Value timestamp = types::Value::timestamp(types::TimestampValue{0});
    const auto expected_text = types::output_value(timestamp);
    ASSERT_TRUE(expected_text);
    auto execution = ExecutionResult::query(
        {ColumnMetadata{.name = "created_at", .data_type = WireDataType::kChar}},
        {{timestamp}}
    );
    ASSERT_TRUE(execution) << execution.error().to_string();
    const RequestResponse response{StreamResponse{.execution = std::move(execution).value()}};

    ASSERT_TRUE(encoder.emit(response, sink));
    ASSERT_EQ(sink.frames.size(), 3U);
    const auto& row = sink.frames[1U];
    ASSERT_EQ(row[4U], std::byte{static_cast<std::uint8_t>(FrameTag::kRow)});
    ASSERT_EQ(row[kRmdbFrameHeaderSize], std::byte{1U});
    const std::size_t payload_offset = kRmdbFrameHeaderSize + 5U;
    ASSERT_EQ(row.size(), payload_offset + expected_text.value().size());
    for (std::size_t index = 0U; index < expected_text.value().size(); ++index) {
        EXPECT_EQ(
            row[payload_offset + index],
            static_cast<std::byte>(static_cast<unsigned char>(expected_text.value()[index]))
        );
    }
}

TEST(ResponseEncoderTest, RejectsInvalidQueryModelsAtConstruction) {
    const auto empty_query = ExecutionResult::query({}, {});
    ASSERT_FALSE(empty_query);
    EXPECT_EQ(empty_query.error().code(), base::ErrorCode::kInvalidArgument);

    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "total", .data_type = WireDataType::kInt32},
    };
    const auto wrong_width = ExecutionResult::query(columns, {{}});
    ASSERT_FALSE(wrong_width);
    EXPECT_EQ(wrong_width.error().code(), base::ErrorCode::kInvalidArgument);

    const auto wrong_type =
        ExecutionResult::query(columns, {{types::Value::character("not an integer")}});
    ASSERT_FALSE(wrong_type);
    EXPECT_EQ(wrong_type.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(ResponseEncoderTest, BoundsDiagnosticsAtUtf8CodePointBoundaries) {
    ResponseEncoder encoder;
    CollectingSink sink;
    std::string diagnostic(kMaximumRmdbDiagnosticSize - 1U, 'a');
    diagnostic.append("\xE2\x82\xAC");
    const base::Error error{base::ErrorCode::kExecution, std::move(diagnostic)};

    ASSERT_TRUE(encoder.emit_terminal_error(TerminalResponseKind::kError, error, sink));
    ASSERT_EQ(sink.frames.size(), 1U);
    const auto& frame = sink.frames.front();
    ASSERT_EQ(frame.size(), kRmdbFrameHeaderSize + kMaximumRmdbDiagnosticSize - 1U);
    EXPECT_EQ(frame[0U], std::byte{0U});
    EXPECT_EQ(frame[1U], std::byte{0U});
    EXPECT_EQ(frame[2U], std::byte{0xFFU});
    EXPECT_EQ(frame[3U], std::byte{0xFFU});
    EXPECT_EQ(frame[4U], std::byte{static_cast<std::uint8_t>(FrameTag::kError)});
    EXPECT_EQ(frame.back(), std::byte{'a'});
}

TEST(ResponseEncoderTest, RetainsReusableBuffersAcrossSmallerResponses) {
    ResponseEncoder encoder;
    CollectingSink sink;
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "total", .data_type = WireDataType::kInt32},
        ColumnMetadata{.name = "note", .data_type = WireDataType::kChar},
    };
    std::vector<std::vector<types::Value>> rows(
        256U,
        {types::Value::integer(42), types::Value::character("hi")}
    );
    auto execution = ExecutionResult::query(columns, std::move(rows));
    ASSERT_TRUE(execution) << execution.error().to_string();
    const RequestResponse query{StreamResponse{.execution = std::move(execution).value()}};

    ASSERT_TRUE(encoder.emit(query, sink));
    const std::size_t frame_capacity = encoder.frame_buffer_capacity();
    const std::size_t payload_capacity = encoder.payload_buffer_capacity();
    ASSERT_GT(frame_capacity, 0U);
    ASSERT_GT(payload_capacity, 0U);

    const RequestResponse command{StreamResponse{.execution = ExecutionResult{}}};
    ASSERT_TRUE(encoder.emit(command, sink));
    EXPECT_GE(encoder.frame_buffer_capacity(), frame_capacity);
    EXPECT_GE(encoder.payload_buffer_capacity(), payload_capacity);
}

}  // namespace
}  // namespace db::protocol
