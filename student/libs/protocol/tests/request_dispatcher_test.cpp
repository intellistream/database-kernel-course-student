#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/protocol/request_dispatcher.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "rmdb_v3_fixture_corpus.hpp"

namespace db::protocol {
namespace {

struct PreparedExpectation final {
    std::string sql;
    std::vector<WireDataType> parameter_types;
    std::vector<ColumnMetadata> columns;
    ExecutionResult execution;
    std::optional<base::Error> execution_error;
};

class RecordingPreparedStatement final : public PreparedStatementPort {
public:
    RecordingPreparedStatement(
        std::vector<WireDataType> parameter_types,
        std::vector<ColumnMetadata> columns,
        ExecutionResult execution,
        std::optional<base::Error> execution_error,
        std::vector<std::vector<types::Value>>& executions
    )
        : parameter_types_{std::move(parameter_types)},
          columns_{std::move(columns)},
          execution_{std::move(execution)},
          execution_error_{std::move(execution_error)},
          executions_{&executions} {}

    auto parameter_types() const noexcept -> std::span<const WireDataType> override {
        return parameter_types_;
    }

    auto result_columns() const noexcept -> std::span<const ColumnMetadata> override {
        return columns_;
    }

    auto execute(const std::span<const types::Value> parameters)
        -> base::Result<ExecutionResult> override {
        executions_->emplace_back(parameters.begin(), parameters.end());
        if (execution_error_.has_value()) {
            return base::fail(*execution_error_);
        }
        return execution_;
    }

private:
    std::vector<WireDataType> parameter_types_;
    std::vector<ColumnMetadata> columns_;
    ExecutionResult execution_;
    std::optional<base::Error> execution_error_;
    std::vector<std::vector<types::Value>>* executions_{nullptr};
};

class RecordingSession final : public SessionPort {
public:
    auto execute_stream(const std::string_view sql) -> base::Result<ExecutionResult> override {
        stream_sql.emplace_back(sql);
        if (stream_error.has_value()) {
            return base::fail(*stream_error);
        }
        return stream_execution;
    }

    auto prepare(const std::string_view sql, const std::span<const WireDataType> parameter_types)
        -> base::Result<std::unique_ptr<PreparedStatementPort>> override {
        prepare_sql.emplace_back(sql);
        prepare_parameter_types.emplace_back(parameter_types.begin(), parameter_types.end());
        if (prepare_failure_call.has_value()
            && prepare_failure_call.value() == prepare_sql.size()) {
            return base::fail(prepare_failure);
        }
        if (next_prepared_expectation_ >= prepared_expectations.size()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "test session received an unexpected PREPARE_SET entry",
                }
            );
        }

        const PreparedExpectation& expected = prepared_expectations[next_prepared_expectation_];
        ++next_prepared_expectation_;
        if (sql != expected.sql
            || !std::equal(
                parameter_types.begin(),
                parameter_types.end(),
                expected.parameter_types.begin(),
                expected.parameter_types.end()
            )) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvariantViolation,
                    "test session received an unexpected prepared statement shape",
                }
            );
        }

        std::unique_ptr<PreparedStatementPort> statement =
            std::make_unique<RecordingPreparedStatement>(
                expected.parameter_types,
                expected.columns,
                expected.execution,
                expected.execution_error,
                prepared_executions
            );
        return statement;
    }

    auto begin() -> base::Status override {
        ++begin_count;
        return begin_status;
    }

    auto commit() -> base::Status override {
        ++commit_count;
        return commit_status;
    }

    auto rollback() -> base::Status override {
        ++rollback_count;
        return rollback_status;
    }

    auto close() -> base::Status override {
        ++close_count;
        return {};
    }

    ExecutionResult stream_execution{};
    std::optional<base::Error> stream_error;
    std::optional<std::size_t> prepare_failure_call;
    base::Error prepare_failure{base::ErrorCode::kBind, "configured test prepare failure"};
    base::Status begin_status{};
    base::Status commit_status{};
    base::Status rollback_status{};
    std::vector<PreparedExpectation> prepared_expectations;
    std::vector<std::string> stream_sql;
    std::vector<std::string> prepare_sql;
    std::vector<std::vector<WireDataType>> prepare_parameter_types;
    std::vector<std::vector<types::Value>> prepared_executions;
    std::size_t begin_count{0U};
    std::size_t commit_count{0U};
    std::size_t rollback_count{0U};
    std::size_t close_count{0U};

private:
    std::size_t next_prepared_expectation_{0U};
};

class ThrowingSession final : public SessionPort {
public:
    auto execute_stream(const std::string_view sql) -> base::Result<ExecutionResult> override {
        static_cast<void>(sql);
        throw std::runtime_error{"configured protocol-port failure"};
    }

    auto prepare(const std::string_view sql, const std::span<const WireDataType> parameter_types)
        -> base::Result<std::unique_ptr<PreparedStatementPort>> override {
        static_cast<void>(sql);
        static_cast<void>(parameter_types);
        return base::fail(
            base::Error{
                base::ErrorCode::kInvariantViolation,
                "throwing test session received prepare",
            }
        );
    }

    auto begin() -> base::Status override {
        return {};
    }

    auto commit() -> base::Status override {
        return {};
    }

    auto rollback() -> base::Status override {
        return {};
    }

    auto close() -> base::Status override {
        return {};
    }
};

struct PrepareDefinition final {
    std::uint16_t statement_id{0U};
    std::uint8_t result_kind{0U};
    std::vector<WireDataType> parameter_types;
    std::string sql;
};

void append_u16(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(value >> 8U)));
    bytes.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(value)));
}

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (const std::uint32_t shift : {24U, 16U, 8U, 0U}) {
        bytes.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(value >> shift)));
    }
}

void append_sql_bytes(std::vector<std::byte>& bytes, const std::string_view sql) {
    for (const char character : sql) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
}

auto encode_prepare_set(const std::vector<PrepareDefinition>& definitions)
    -> std::vector<std::byte> {
    std::vector<std::byte> bytes;
    append_u16(bytes, static_cast<std::uint16_t>(definitions.size()));
    for (const PrepareDefinition& definition : definitions) {
        append_u16(bytes, definition.statement_id);
        bytes.push_back(static_cast<std::byte>(definition.result_kind));
        append_u16(bytes, static_cast<std::uint16_t>(definition.parameter_types.size()));
        for (const WireDataType parameter_type : definition.parameter_types) {
            bytes.push_back(static_cast<std::byte>(parameter_type));
        }
        append_u32(bytes, static_cast<std::uint32_t>(definition.sql.size()));
        append_sql_bytes(bytes, definition.sql);
    }
    return bytes;
}

auto frame(
    const FrameTag tag,
    const std::vector<std::byte>& payload,
    const std::uint8_t flags = kRmdbNoFrameFlags
) -> FrameView {
    return FrameView{
        .header =
            FrameHeader{
                .payload_length = static_cast<std::uint32_t>(payload.size()),
                .tag = static_cast<std::uint8_t>(tag),
                .flags = flags,
            },
        .payload = base::Bytes{payload.data(), payload.size()},
    };
}

auto fixture_frame(const base::Bytes wire) -> FrameView {
    base::BigEndianReader reader{wire.first(kRmdbFrameHeaderSize)};
    const auto payload_length = reader.read_u32();
    const auto tag = reader.read_u8();
    const auto flags = reader.read_u8();
    const auto reserved = reader.read_u16();
    EXPECT_TRUE(payload_length);
    EXPECT_TRUE(tag);
    EXPECT_TRUE(flags);
    EXPECT_TRUE(reserved);
    EXPECT_TRUE(reader.finish());
    return FrameView{
        .header =
            FrameHeader{
                .payload_length = payload_length.value(),
                .tag = tag.value(),
                .flags = flags.value(),
                .reserved = reserved.value(),
            },
        .payload = wire.subspan(kRmdbFrameHeaderSize),
    };
}

auto query_result(std::vector<ColumnMetadata> columns, std::vector<std::vector<types::Value>> rows)
    -> ExecutionResult {
    auto result = ExecutionResult::query(std::move(columns), std::move(rows));
    if (!result) {
        ADD_FAILURE() << result.error().to_string();
        std::terminate();
    }
    return std::move(result).value();
}

auto make_dispatcher(SessionPort& session) -> RequestDispatcher {
    auto dispatcher = RequestDispatcher::create(session);
    if (!dispatcher) {
        ADD_FAILURE() << dispatcher.error().to_string();
        std::terminate();
    }
    return std::move(dispatcher).value();
}

void expect_inactive(RequestDispatcher& dispatcher) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(dispatcher.valid());
    EXPECT_EQ(dispatcher.prepared_statement_count(), 0U);

    const std::vector<std::byte> payload;
    const auto dispatched = dispatcher.dispatch(frame(FrameTag::kExecStream, payload));
    ASSERT_FALSE(dispatched);
    EXPECT_EQ(dispatched.error().code(), base::ErrorCode::kUnavailable);
}

void install_fixture_statement(RecordingSession& session, RequestDispatcher& dispatcher) {
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "total", .data_type = WireDataType::kInt32},
        ColumnMetadata{.name = "note", .data_type = WireDataType::kChar},
    };
    session.prepared_expectations.push_back(
        PreparedExpectation{
            .sql = "SELECT $1, $2;",
            .parameter_types = {WireDataType::kInt32, WireDataType::kChar},
            .columns = columns,
            .execution =
                query_result(columns, {{types::Value::integer(42), types::Value::character("hi")}}),
            .execution_error = std::nullopt,
        }
    );
    const auto response =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kPrepareSetTwoParameters));
    ASSERT_TRUE(response);
    ASSERT_TRUE(std::holds_alternative<PrepareResponse>(response.value()));
}

TEST(RequestDispatcherTest, ExecutesValidatedStreamSqlAndReportsSemanticQueryResult) {
    RecordingSession session;
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "one", .data_type = WireDataType::kInt32},
    };
    session.stream_execution = query_result(columns, {{types::Value::integer(1)}});
    RequestDispatcher dispatcher = make_dispatcher(session);

    const auto response =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kExecStreamSelectOne));
    ASSERT_TRUE(response);
    const auto* const stream = std::get_if<StreamResponse>(&response.value());
    ASSERT_NE(stream, nullptr);
    EXPECT_EQ(stream->execution, session.stream_execution);
    ASSERT_EQ(session.stream_sql.size(), 1U);
    EXPECT_EQ(session.stream_sql.front(), "SELECT 1;");

    const std::vector<std::byte> empty;
    const auto invalid = dispatcher.dispatch(frame(FrameTag::kExecStream, empty));
    ASSERT_TRUE(invalid);
    const auto* const terminal = std::get_if<TerminalErrorResponse>(&invalid.value());
    ASSERT_NE(terminal, nullptr);
    EXPECT_EQ(terminal->kind, TerminalResponseKind::kError);
    EXPECT_EQ(terminal->error.code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(session.stream_sql.size(), 1U);
}

TEST(RequestDispatcherTest, MoveTransfersConnectionStateAndLeavesTheSourceInert) {
    RecordingSession session;
    RequestDispatcher source = make_dispatcher(session);
    EXPECT_TRUE(source.valid());

    RequestDispatcher moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive(source);

    RequestDispatcher assigned = make_dispatcher(session);
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive(moved);
}

TEST(RequestDispatcherTest, ConvertsEscapedPortExceptionsAtTheDispatchBoundary) {
    ThrowingSession session;
    RequestDispatcher dispatcher = make_dispatcher(session);

    const auto response =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kExecStreamSelectOne));
    ASSERT_FALSE(response);
    EXPECT_EQ(response.error().code(), base::ErrorCode::kInternal);
    ASSERT_EQ(response.error().contexts().size(), 1U);
    EXPECT_EQ(response.error().contexts().front().component, "protocol.request_dispatcher");
    EXPECT_EQ(response.error().contexts().front().operation, "dispatch request");
}

TEST(RequestDispatcherTest, RetryableStreamFailureRollsBackBeforeTransactionAbort) {
    RecordingSession session;
    session.stream_error = base::Error{
        base::ErrorCode::kSerializationFailure,
        "concurrent update conflict",
    };
    RequestDispatcher dispatcher = make_dispatcher(session);

    const auto response =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kExecStreamSelectOne));
    ASSERT_TRUE(response);
    const auto* const terminal = std::get_if<TerminalErrorResponse>(&response.value());
    ASSERT_NE(terminal, nullptr);
    EXPECT_EQ(terminal->kind, TerminalResponseKind::kTransactionAbort);
    EXPECT_EQ(terminal->error.code(), base::ErrorCode::kSerializationFailure);
    EXPECT_EQ(session.rollback_count, 1U);
}

TEST(RequestDispatcherTest, PrepareSetAtomicallyReplacesTheConnectionDictionary) {
    RecordingSession session;
    RequestDispatcher dispatcher = make_dispatcher(session);
    install_fixture_statement(session, dispatcher);
    EXPECT_EQ(dispatcher.prepared_statement_count(), 1U);
    ASSERT_EQ(session.prepare_sql.size(), 1U);

    const std::vector<std::byte> duplicate_ids = encode_prepare_set({
        PrepareDefinition{
            .statement_id = 9U,
            .result_kind = 1U,
            .parameter_types = {},
            .sql = "SELECT 1;",
        },
        PrepareDefinition{
            .statement_id = 9U,
            .result_kind = 1U,
            .parameter_types = {},
            .sql = "SELECT 1;",
        },
    });
    const auto rejected = dispatcher.dispatch(frame(FrameTag::kPrepareSet, duplicate_ids));
    ASSERT_TRUE(rejected);
    const auto* const terminal = std::get_if<TerminalErrorResponse>(&rejected.value());
    ASSERT_NE(terminal, nullptr);
    EXPECT_EQ(terminal->kind, TerminalResponseKind::kError);
    EXPECT_EQ(terminal->error.code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(dispatcher.prepared_statement_count(), 1U);
    EXPECT_EQ(session.prepare_sql.size(), 1U);

    const auto batch =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kExecBatchTwoParameters));
    ASSERT_TRUE(batch);
    EXPECT_TRUE(std::holds_alternative<BatchSuccessResponse>(batch.value()));
    ASSERT_EQ(session.prepared_executions.size(), 1U);
}

TEST(RequestDispatcherTest, PrepareSetRetainsTheOldDictionaryWhenLaterBindingFails) {
    RecordingSession session;
    RequestDispatcher dispatcher = make_dispatcher(session);
    install_fixture_statement(session, dispatcher);

    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "value", .data_type = WireDataType::kInt32},
    };
    session.prepared_expectations.push_back(
        PreparedExpectation{
            .sql = "SELECT $1;",
            .parameter_types = {WireDataType::kInt32},
            .columns = columns,
            .execution = query_result(columns, {{types::Value::integer(1)}}),
            .execution_error = std::nullopt,
        }
    );
    session.prepare_failure_call = 3U;
    session.prepare_failure = base::Error{base::ErrorCode::kBind, "second candidate bind failed"};
    const std::vector<std::byte> candidate = encode_prepare_set({
        PrepareDefinition{
            .statement_id = 20U,
            .result_kind = 1U,
            .parameter_types = {WireDataType::kInt32},
            .sql = "SELECT $1;",
        },
        PrepareDefinition{
            .statement_id = 21U,
            .result_kind = 1U,
            .parameter_types = {WireDataType::kInt32},
            .sql = "SELECT $1;",
        },
    });
    const auto rejected = dispatcher.dispatch(frame(FrameTag::kPrepareSet, candidate));
    ASSERT_TRUE(rejected);
    const auto* const terminal = std::get_if<TerminalErrorResponse>(&rejected.value());
    ASSERT_NE(terminal, nullptr);
    EXPECT_EQ(terminal->kind, TerminalResponseKind::kError);
    EXPECT_EQ(terminal->error.code(), base::ErrorCode::kBind);
    EXPECT_EQ(dispatcher.prepared_statement_count(), 1U);
    EXPECT_EQ(session.prepare_sql.size(), 3U);

    const auto old_statement =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kExecBatchTwoParameters));
    ASSERT_TRUE(old_statement);
    EXPECT_TRUE(std::holds_alternative<BatchSuccessResponse>(old_statement.value()));
}

TEST(RequestDispatcherTest, PrepareSetIgnoresParameterLikeTextInsideQuotedStrings) {
    RecordingSession session;
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "value", .data_type = WireDataType::kInt32},
    };
    session.prepared_expectations.push_back(
        PreparedExpectation{
            .sql = "SELECT '$4' WHERE a=$1 AND b=$2;",
            .parameter_types = {WireDataType::kInt32, WireDataType::kInt32},
            .columns = columns,
            .execution = query_result(columns, {{types::Value::integer(1)}}),
            .execution_error = std::nullopt,
        }
    );
    RequestDispatcher dispatcher = make_dispatcher(session);
    const std::vector<std::byte> payload = encode_prepare_set({
        PrepareDefinition{
            .statement_id = 12U,
            .result_kind = 1U,
            .parameter_types = {WireDataType::kInt32, WireDataType::kInt32},
            .sql = "SELECT '$4' WHERE a=$1 AND b=$2;",
        },
    });

    const auto response = dispatcher.dispatch(frame(FrameTag::kPrepareSet, payload));
    ASSERT_TRUE(response);
    EXPECT_TRUE(std::holds_alternative<PrepareResponse>(response.value()));
    ASSERT_EQ(session.prepare_sql.size(), 1U);
    EXPECT_EQ(session.prepare_sql.front(), "SELECT '$4' WHERE a=$1 AND b=$2;");
}

TEST(RequestDispatcherTest, DispatchesFixtureBatchAndPreservesTypedParameters) {
    RecordingSession session;
    RequestDispatcher dispatcher = make_dispatcher(session);
    install_fixture_statement(session, dispatcher);

    const auto response =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kExecBatchTwoParameters));
    ASSERT_TRUE(response);
    const auto* const batch = std::get_if<BatchSuccessResponse>(&response.value());
    ASSERT_NE(batch, nullptr);
    EXPECT_EQ(batch->executed_operation_count, 1U);
    ASSERT_EQ(batch->query_results.size(), 1U);
    EXPECT_EQ(batch->query_results.front().operation_index, 0U);
    ASSERT_EQ(session.prepared_executions.size(), 1U);
    ASSERT_EQ(session.prepared_executions.front().size(), 2U);
    const auto* const integer = session.prepared_executions.front()[0U].as_integer();
    ASSERT_NE(integer, nullptr);
    EXPECT_EQ(*integer, 42);
    const auto* const character = session.prepared_executions.front()[1U].as_string();
    ASSERT_NE(character, nullptr);
    EXPECT_EQ(*character, "hi");
}

TEST(RequestDispatcherTest, RejectsResultsThatWouldNarrowAnInt32ResponseValueAtConstruction) {
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "too_large", .data_type = WireDataType::kInt32},
    };
    auto result = ExecutionResult::query(
        columns,
        {{types::Value::integer(
            static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1
        )}}
    );
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kOutOfRange);
}

TEST(RequestDispatcherTest, AcceptsTimestampResultsThroughThePublishedCharMapping) {
    RecordingSession session;
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "created_at", .data_type = WireDataType::kChar},
    };
    session.stream_execution =
        query_result(columns, {{types::Value::timestamp(types::TimestampValue{0})}});
    RequestDispatcher dispatcher = make_dispatcher(session);

    const auto response =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kExecStreamSelectOne));
    ASSERT_TRUE(response);
    const auto* const stream = std::get_if<StreamResponse>(&response.value());
    ASSERT_NE(stream, nullptr);
    EXPECT_EQ(stream->execution, session.stream_execution);
}

TEST(RequestDispatcherTest, DecodesIntFloatAndNullCharParametersWithoutNarrowing) {
    RecordingSession session;
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "value", .data_type = WireDataType::kInt32},
    };
    session.prepared_expectations.push_back(
        PreparedExpectation{
            .sql = "SELECT $1, $2, $3;",
            .parameter_types =
                {
                    WireDataType::kInt32,
                    WireDataType::kFloat32,
                    WireDataType::kChar,
                },
            .columns = columns,
            .execution = query_result(columns, {{types::Value::integer(1)}}),
            .execution_error = std::nullopt,
        }
    );
    RequestDispatcher dispatcher = make_dispatcher(session);

    const std::vector<std::byte> prepared = encode_prepare_set({
        PrepareDefinition{
            .statement_id = 9U,
            .result_kind = 1U,
            .parameter_types =
                {
                    WireDataType::kInt32,
                    WireDataType::kFloat32,
                    WireDataType::kChar,
                },
            .sql = "SELECT $1, $2, $3;",
        },
    });
    ASSERT_TRUE(dispatcher.dispatch(frame(FrameTag::kPrepareSet, prepared)));

    std::vector<std::byte> batch_payload;
    append_u16(batch_payload, 1U);
    append_u16(batch_payload, 9U);
    batch_payload.push_back(std::byte{1U});
    append_u32(batch_payload, std::bit_cast<std::uint32_t>(std::int32_t{-7}));
    batch_payload.push_back(std::byte{1U});
    append_u32(batch_payload, 0x80000000U);
    batch_payload.push_back(std::byte{0U});

    const auto response =
        dispatcher.dispatch(frame(FrameTag::kExecBatch, batch_payload, kRmdbAutoAbortFrameFlag));
    ASSERT_TRUE(response);
    EXPECT_TRUE(std::holds_alternative<BatchSuccessResponse>(response.value()));
    ASSERT_EQ(session.prepared_executions.size(), 1U);
    const auto& parameters = session.prepared_executions.front();
    ASSERT_EQ(parameters.size(), 3U);
    ASSERT_NE(parameters[0U].as_integer(), nullptr);
    EXPECT_EQ(*parameters[0U].as_integer(), -7);
    ASSERT_NE(parameters[1U].as_float(), nullptr);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(*parameters[1U].as_float()), 0x80000000U);
    EXPECT_TRUE(parameters[2U].is_null());
    EXPECT_EQ(parameters[2U].data_type(), types::kCharType);
}

TEST(RequestDispatcherTest, ExecutesPreparedTransactionControlsInBatchOrder) {
    RecordingSession session;
    RequestDispatcher dispatcher = make_dispatcher(session);
    const std::vector<std::byte> prepared = encode_prepare_set({
        PrepareDefinition{
            .statement_id = 1U,
            .result_kind = 0U,
            .parameter_types = {},
            .sql = "BEGIN;",
        },
        PrepareDefinition{
            .statement_id = 2U,
            .result_kind = 0U,
            .parameter_types = {},
            .sql = "COMMIT;",
        },
        PrepareDefinition{
            .statement_id = 3U,
            .result_kind = 0U,
            .parameter_types = {},
            .sql = "ROLLBACK;",
        },
    });
    const auto installed = dispatcher.dispatch(frame(FrameTag::kPrepareSet, prepared));
    ASSERT_TRUE(installed);
    const auto* const prepare = std::get_if<PrepareResponse>(&installed.value());
    ASSERT_NE(prepare, nullptr);
    ASSERT_EQ(prepare->statements.size(), 3U);
    EXPECT_TRUE(prepare->statements[0U].columns.empty());
    EXPECT_TRUE(prepare->statements[1U].columns.empty());
    EXPECT_TRUE(prepare->statements[2U].columns.empty());
    EXPECT_TRUE(session.prepare_sql.empty());

    std::vector<std::byte> batch_payload;
    append_u16(batch_payload, 3U);
    append_u16(batch_payload, 1U);
    append_u16(batch_payload, 2U);
    append_u16(batch_payload, 3U);
    const auto response =
        dispatcher.dispatch(frame(FrameTag::kExecBatch, batch_payload, kRmdbAutoAbortFrameFlag));
    ASSERT_TRUE(response);
    const auto* const batch = std::get_if<BatchSuccessResponse>(&response.value());
    ASSERT_NE(batch, nullptr);
    EXPECT_EQ(batch->executed_operation_count, 3U);
    EXPECT_EQ(session.begin_count, 1U);
    EXPECT_EQ(session.commit_count, 1U);
    EXPECT_EQ(session.rollback_count, 1U);
}

TEST(RequestDispatcherTest, MalformedBatchRollsBackAndRetainsThePreparedDictionary) {
    RecordingSession session;
    RequestDispatcher dispatcher = make_dispatcher(session);
    install_fixture_statement(session, dispatcher);

    std::vector<std::byte> unknown_statement;
    append_u16(unknown_statement, 1U);
    append_u16(unknown_statement, 8U);
    const auto rejected = dispatcher.dispatch(
        frame(FrameTag::kExecBatch, unknown_statement, kRmdbAutoAbortFrameFlag)
    );
    ASSERT_TRUE(rejected);
    const auto* const terminal = std::get_if<TerminalErrorResponse>(&rejected.value());
    ASSERT_NE(terminal, nullptr);
    EXPECT_EQ(terminal->kind, TerminalResponseKind::kError);
    EXPECT_EQ(terminal->error.code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(session.rollback_count, 1U);
    EXPECT_EQ(dispatcher.prepared_statement_count(), 1U);

    std::vector<std::byte> invalid_utf8;
    append_u16(invalid_utf8, 1U);
    append_u16(invalid_utf8, 7U);
    invalid_utf8.push_back(std::byte{1U});
    append_u32(invalid_utf8, 42U);
    invalid_utf8.push_back(std::byte{1U});
    append_u32(invalid_utf8, 1U);
    invalid_utf8.push_back(std::byte{0xFFU});
    const auto invalid_text =
        dispatcher.dispatch(frame(FrameTag::kExecBatch, invalid_utf8, kRmdbAutoAbortFrameFlag));
    ASSERT_TRUE(invalid_text);
    const auto* const text_terminal = std::get_if<TerminalErrorResponse>(&invalid_text.value());
    ASSERT_NE(text_terminal, nullptr);
    EXPECT_EQ(text_terminal->kind, TerminalResponseKind::kError);
    EXPECT_EQ(text_terminal->error.code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(session.rollback_count, 2U);

    const auto valid =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kExecBatchTwoParameters));
    ASSERT_TRUE(valid);
    EXPECT_TRUE(std::holds_alternative<BatchSuccessResponse>(valid.value()));
}

TEST(RequestDispatcherTest, BatchExecutionFailureRollsBackAndPreservesRetryability) {
    RecordingSession session;
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "total", .data_type = WireDataType::kInt32},
        ColumnMetadata{.name = "note", .data_type = WireDataType::kChar},
    };
    session.prepared_expectations.push_back(
        PreparedExpectation{
            .sql = "SELECT $1, $2;",
            .parameter_types = {WireDataType::kInt32, WireDataType::kChar},
            .columns = columns,
            .execution = query_result(columns, {}),
            .execution_error = base::Error{
                base::ErrorCode::kSerializationFailure,
                "concurrent update conflict",
            },
        }
    );
    RequestDispatcher dispatcher = make_dispatcher(session);
    const auto installed =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kPrepareSetTwoParameters));
    ASSERT_TRUE(installed);
    ASSERT_TRUE(std::holds_alternative<PrepareResponse>(installed.value()));

    const auto response =
        dispatcher.dispatch(fixture_frame(test::rmdb_v3_fixture::kExecBatchTwoParameters));
    ASSERT_TRUE(response);
    const auto* const failure = std::get_if<BatchFailureResponse>(&response.value());
    ASSERT_NE(failure, nullptr);
    EXPECT_EQ(failure->executed_operation_count, 0U);
    EXPECT_EQ(failure->failed_operation, 0U);
    EXPECT_TRUE(failure->retryable);
    EXPECT_EQ(failure->error.code(), base::ErrorCode::kSerializationFailure);
    EXPECT_EQ(session.rollback_count, 1U);
}

TEST(RequestDispatcherTest, FatalBatchPortViolationsStillRollBackBeforeReturningAnInternalError) {
    RecordingSession session;
    const std::vector<ColumnMetadata> columns{
        ColumnMetadata{.name = "value", .data_type = WireDataType::kInt32},
    };
    session.prepared_expectations.push_back(
        PreparedExpectation{
            .sql = "SELECT $1;",
            .parameter_types = {WireDataType::kInt32},
            .columns = columns,
            .execution = ExecutionResult{},
            .execution_error = std::nullopt,
        }
    );
    RequestDispatcher dispatcher = make_dispatcher(session);
    const std::vector<std::byte> prepared = encode_prepare_set({
        PrepareDefinition{
            .statement_id = 3U,
            .result_kind = 1U,
            .parameter_types = {WireDataType::kInt32},
            .sql = "SELECT $1;",
        },
    });
    ASSERT_TRUE(dispatcher.dispatch(frame(FrameTag::kPrepareSet, prepared)));

    std::vector<std::byte> batch_payload;
    append_u16(batch_payload, 1U);
    append_u16(batch_payload, 3U);
    batch_payload.push_back(std::byte{1U});
    append_u32(batch_payload, 1U);
    const auto response =
        dispatcher.dispatch(frame(FrameTag::kExecBatch, batch_payload, kRmdbAutoAbortFrameFlag));
    ASSERT_FALSE(response);
    EXPECT_EQ(response.error().code(), base::ErrorCode::kInvariantViolation);
    EXPECT_EQ(session.rollback_count, 1U);
}

TEST(RequestDispatcherTest, PrepareSetRejectsMissingParameterOrdinalBeforeBinding) {
    RecordingSession session;
    RequestDispatcher dispatcher = make_dispatcher(session);
    const std::vector<std::byte> payload = encode_prepare_set({
        PrepareDefinition{
            .statement_id = 4U,
            .result_kind = 1U,
            .parameter_types = {WireDataType::kInt32, WireDataType::kInt32},
            .sql = "SELECT $2;",
        },
    });

    const auto rejected = dispatcher.dispatch(frame(FrameTag::kPrepareSet, payload));
    ASSERT_TRUE(rejected);
    const auto* const terminal = std::get_if<TerminalErrorResponse>(&rejected.value());
    ASSERT_NE(terminal, nullptr);
    EXPECT_EQ(terminal->kind, TerminalResponseKind::kError);
    EXPECT_EQ(terminal->error.code(), base::ErrorCode::kInvalidArgument);
    EXPECT_TRUE(session.prepare_sql.empty());
    EXPECT_EQ(dispatcher.prepared_statement_count(), 0U);
}

}  // namespace
}  // namespace db::protocol
