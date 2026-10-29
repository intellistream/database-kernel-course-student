#include "db/protocol/response_encoder.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/exception_boundary.hpp"
#include "db/base/text.hpp"
#include "db/protocol/frame_codec.hpp"

#include "network_byte_appender.hpp"

namespace db::protocol {
namespace {

inline constexpr std::uint8_t kBatchStatusOk = 0U;
inline constexpr std::uint8_t kBatchStatusTransactionAbort = 1U;
inline constexpr std::uint8_t kBatchStatusError = 2U;
inline constexpr std::uint16_t kNoFailedBatchOperation = std::numeric_limits<std::uint16_t>::max();
inline constexpr std::string_view kInvalidDiagnostic = "RMDB server diagnostic was not valid UTF-8";
inline constexpr base::ExceptionBoundaryPolicy kEncoderExceptionBoundary{
    .component = "protocol.response_encoder",
    .allocation_message = "RMDB response encoding could not allocate bounded state",
    .exception_prefix = "unexpected exception escaped an RMDB response sink: ",
    .unknown_exception_message = "unknown exception escaped an RMDB response sink",
};

auto encoder_error(
    const base::ErrorCode code,
    std::string message,
    const std::string_view operation
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "protocol.response_encoder",
        std::string{operation}
    );
}

auto invariant_error(std::string message, const std::string_view operation) -> base::Error {
    return encoder_error(base::ErrorCode::kInvariantViolation, std::move(message), operation);
}

auto append_columns(
    std::vector<std::byte>& output,
    const std::span<const ColumnMetadata> columns,
    const bool require_nonempty,
    const std::string_view operation
) -> base::Status {
    if (columns.size() > std::numeric_limits<std::uint16_t>::max()) {
        return base::fail(invariant_error("RMDB response has too many result columns", operation));
    }
    if (require_nonempty && columns.empty()) {
        return base::fail(invariant_error("RMDB query response has no result columns", operation));
    }
    internal::append_big_endian(output, static_cast<std::uint16_t>(columns.size()));
    for (const ColumnMetadata& column : columns) {
        if (column.name.empty()) {
            return base::fail(
                invariant_error("RMDB response has an empty result column name", operation)
            );
        }
        if (column.name.size() > std::numeric_limits<std::uint16_t>::max()) {
            return base::fail(
                invariant_error("RMDB response has an oversized result column name", operation)
            );
        }
        if (!base::is_valid_utf8(column.name)) {
            return base::fail(
                invariant_error("RMDB response has a non-UTF-8 result column name", operation)
            );
        }
        if (!is_known_wire_data_type(column.data_type)) {
            return base::fail(
                invariant_error("RMDB response has an unknown result column type", operation)
            );
        }
        internal::append_big_endian(output, static_cast<std::uint16_t>(column.name.size()));
        internal::append_text_bytes(output, column.name);
        internal::append_big_endian(output, static_cast<std::uint8_t>(column.data_type));
    }
    return {};
}

auto append_int32_cell(std::vector<std::byte>& output, const WireCell& value) -> base::Status {
    if (value.is_null()) {
        internal::append_big_endian(output, std::uint8_t{0U});
        return {};
    }
    internal::append_big_endian(output, std::uint8_t{1U});
    internal::append_big_endian(output, std::bit_cast<std::uint32_t>(value.integer()));
    return {};
}

auto append_float32_cell(std::vector<std::byte>& output, const WireCell& value) -> base::Status {
    if (value.is_null()) {
        internal::append_big_endian(output, std::uint8_t{0U});
        return {};
    }
    internal::append_big_endian(output, std::uint8_t{1U});
    internal::append_big_endian(output, std::bit_cast<std::uint32_t>(value.floating()));
    return {};
}

auto append_char_cell(std::vector<std::byte>& output, const WireCell& value) -> base::Status {
    if (value.is_null()) {
        internal::append_big_endian(output, std::uint8_t{0U});
        return {};
    }
    const std::string_view text = value.text();
    internal::append_big_endian(output, std::uint8_t{1U});
    internal::append_big_endian(output, static_cast<std::uint32_t>(text.size()));
    internal::append_text_bytes(output, text);
    return {};
}

auto append_cell(
    std::vector<std::byte>& output,
    const WireDataType data_type,
    const WireCell& value,
    const std::string_view operation
) -> base::Status {
    switch (data_type) {
        case WireDataType::kInt32:
            return append_int32_cell(output, value);
        case WireDataType::kFloat32:
            return append_float32_cell(output, value);
        case WireDataType::kChar:
            return append_char_cell(output, value);
    }
    return base::fail(invariant_error("RMDB response selected an unknown cell type", operation));
}

auto append_row(
    std::vector<std::byte>& output,
    const std::span<const ColumnMetadata> columns,
    const std::span<const WireCell> row,
    const std::string_view operation
) -> base::Status {
    for (std::size_t index = 0U; index < row.size(); ++index) {
        auto cell = append_cell(output, columns[index].data_type, row[index], operation);
        if (!cell) {
            return base::fail(std::move(cell).error());
        }
    }
    return {};
}

}  // namespace

struct ResponseEncoder::Impl final {
    FrameEncoder frame_encoder;
    std::vector<std::byte> payload;
    std::vector<std::byte> diagnostic;
};

namespace {

auto emit_frame(
    ResponseEncoder::Impl& implementation,
    ResponseSink& sink,
    const FrameTag tag,
    const base::Bytes payload
) -> base::Status {
    auto encoded = implementation.frame_encoder
                       .encode(FrameDirection::kServerToClient, tag, kRmdbNoFrameFlags, payload);
    if (!encoded) {
        return base::fail(
            std::move(encoded).error().with_context(
                "protocol.response_encoder",
                "encode response frame"
            )
        );
    }
    auto written = sink.write_encoded_frame(encoded.value());
    if (!written) {
        return base::fail(
            std::move(written).error().with_context(
                "protocol.response_encoder",
                "write response frame"
            )
        );
    }
    return {};
}

auto emit_stream(
    ResponseEncoder::Impl& implementation,
    ResponseSink& sink,
    const StreamResponse& response
) -> base::Status {
    switch (response.execution.kind()) {
        case ExecutionKind::kCommand: {
            return emit_frame(implementation, sink, FrameTag::kCommandOk, {});
        }
        case ExecutionKind::kQuery: {
            implementation.payload.clear();
            auto columns = append_columns(
                implementation.payload,
                response.execution.columns(),
                true,
                "encode META"
            );
            if (!columns) {
                return base::fail(std::move(columns).error());
            }
            auto meta = emit_frame(
                implementation,
                sink,
                FrameTag::kMeta,
                base::Bytes{implementation.payload.data(), implementation.payload.size()}
            );
            if (!meta) {
                return base::fail(std::move(meta).error());
            }
            for (const std::vector<WireCell>& row : response.execution.rows()) {
                implementation.payload.clear();
                auto encoded_row = append_row(
                    implementation.payload,
                    response.execution.columns(),
                    row,
                    "encode ROW"
                );
                if (!encoded_row) {
                    return base::fail(std::move(encoded_row).error());
                }
                auto emitted = emit_frame(
                    implementation,
                    sink,
                    FrameTag::kRow,
                    base::Bytes{implementation.payload.data(), implementation.payload.size()}
                );
                if (!emitted) {
                    return base::fail(std::move(emitted).error());
                }
            }
            implementation.payload.clear();
            const std::uint64_t row_count{response.execution.rows().size()};
            internal::append_big_endian(implementation.payload, row_count);
            return emit_frame(
                implementation,
                sink,
                FrameTag::kResultEnd,
                base::Bytes{implementation.payload.data(), implementation.payload.size()}
            );
        }
    }
    return base::fail(
        invariant_error("RMDB stream response had an unknown execution kind", "emit")
    );
}

auto emit_prepare(
    ResponseEncoder::Impl& implementation,
    ResponseSink& sink,
    const PrepareResponse& response
) -> base::Status {
    if (response.statements.empty()
        || response.statements.size() > kMaximumRmdbPreparedStatements) {
        return base::fail(invariant_error(
            "PREPARE_OK response statement count is outside 1..=256",
            "encode PREPARE_OK"
        ));
    }

    implementation.payload.clear();
    internal::append_big_endian(
        implementation.payload,
        static_cast<std::uint16_t>(response.statements.size())
    );
    std::vector<std::uint16_t> statement_ids;
    statement_ids.reserve(response.statements.size());
    for (const PreparedStatementMetadata& statement : response.statements) {
        if (statement.statement_id == 0U) {
            return base::fail(
                invariant_error("PREPARE_OK response used statement id zero", "encode PREPARE_OK")
            );
        }
        for (const std::uint16_t previous : statement_ids) {
            if (previous == statement.statement_id) {
                return base::fail(invariant_error(
                    "PREPARE_OK response used duplicate statement ids",
                    "encode PREPARE_OK"
                ));
            }
        }
        statement_ids.push_back(statement.statement_id);
        internal::append_big_endian(implementation.payload, statement.statement_id);
        auto columns =
            append_columns(implementation.payload, statement.columns, false, "encode PREPARE_OK");
        if (!columns) {
            return base::fail(std::move(columns).error());
        }
    }
    return emit_frame(
        implementation,
        sink,
        FrameTag::kPrepareOk,
        base::Bytes{implementation.payload.data(), implementation.payload.size()}
    );
}

auto append_batch_header(
    std::vector<std::byte>& payload,
    const std::uint16_t executed_operation_count,
    const std::uint8_t status,
    const std::uint16_t failed_operation,
    const base::Bytes diagnostic
) -> base::Status {
    if (diagnostic.size() > std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(
            invariant_error("BATCH_RESULT diagnostic exceeded u32 length", "encode BATCH_RESULT")
        );
    }
    internal::append_big_endian(payload, executed_operation_count);
    internal::append_big_endian(payload, status);
    internal::append_big_endian(payload, failed_operation);
    internal::append_big_endian(payload, static_cast<std::uint32_t>(diagnostic.size()));
    internal::append_bytes(payload, diagnostic);
    return {};
}

auto bounded_diagnostic(ResponseEncoder::Impl& implementation, const base::Error& error)
    -> base::Bytes {
    const std::string rendered = error.to_string();
    const auto prefix = base::valid_utf8_prefix_size(rendered, kMaximumRmdbDiagnosticSize);
    const std::string_view diagnostic =
        prefix.has_value() ? std::string_view{rendered}.substr(0U, *prefix) : kInvalidDiagnostic;
    implementation.diagnostic.clear();
    internal::append_text_bytes(implementation.diagnostic, diagnostic);
    return base::Bytes{implementation.diagnostic.data(), implementation.diagnostic.size()};
}

auto emit_batch_success(
    ResponseEncoder::Impl& implementation,
    ResponseSink& sink,
    const BatchSuccessResponse& response
) -> base::Status {
    if (response.executed_operation_count == 0U
        || response.executed_operation_count > kMaximumRmdbBatchOperations
        || response.query_results.size() > response.executed_operation_count) {
        return base::fail(invariant_error(
            "BATCH_RESULT success has an invalid operation or query-result count",
            "encode BATCH_RESULT"
        ));
    }

    implementation.payload.clear();
    auto header = append_batch_header(
        implementation.payload,
        response.executed_operation_count,
        kBatchStatusOk,
        kNoFailedBatchOperation,
        {}
    );
    if (!header) {
        return base::fail(std::move(header).error());
    }
    if (response.query_results.size() > std::numeric_limits<std::uint16_t>::max()) {
        return base::fail(
            invariant_error("BATCH_RESULT has too many query results", "encode BATCH_RESULT")
        );
    }
    internal::append_big_endian(
        implementation.payload,
        static_cast<std::uint16_t>(response.query_results.size())
    );

    std::optional<std::uint16_t> previous_index;
    for (const BatchQueryResult& query_result : response.query_results) {
        if (query_result.operation_index >= response.executed_operation_count
            || (previous_index.has_value() && query_result.operation_index <= *previous_index)) {
            return base::fail(invariant_error(
                "BATCH_RESULT query result indices were not strictly increasing",
                "encode BATCH_RESULT"
            ));
        }
        if (query_result.execution.kind() != ExecutionKind::kQuery) {
            return base::fail(invariant_error(
                "BATCH_RESULT contained a command in its query results",
                "encode BATCH_RESULT"
            ));
        }
        if (query_result.execution.rows().size() > std::numeric_limits<std::uint32_t>::max()) {
            return base::fail(
                invariant_error("BATCH_RESULT query row count exceeded u32", "encode BATCH_RESULT")
            );
        }
        internal::append_big_endian(implementation.payload, query_result.operation_index);
        internal::append_big_endian(
            implementation.payload,
            static_cast<std::uint32_t>(query_result.execution.rows().size())
        );
        for (const std::vector<WireCell>& row : query_result.execution.rows()) {
            auto encoded = append_row(
                implementation.payload,
                query_result.execution.columns(),
                row,
                "encode BATCH_RESULT"
            );
            if (!encoded) {
                return base::fail(std::move(encoded).error());
            }
        }
        previous_index = query_result.operation_index;
    }
    return emit_frame(
        implementation,
        sink,
        FrameTag::kBatchResult,
        base::Bytes{implementation.payload.data(), implementation.payload.size()}
    );
}

auto emit_batch_failure(
    ResponseEncoder::Impl& implementation,
    ResponseSink& sink,
    const BatchFailureResponse& response
) -> base::Status {
    if (response.executed_operation_count != response.failed_operation
        || response.failed_operation >= kMaximumRmdbBatchOperations) {
        return base::fail(invariant_error(
            "BATCH_RESULT failure did not name one bounded failed operation",
            "encode BATCH_RESULT"
        ));
    }
    const base::Bytes diagnostic = bounded_diagnostic(implementation, response.error);
    implementation.payload.clear();
    auto header = append_batch_header(
        implementation.payload,
        response.executed_operation_count,
        response.retryable ? kBatchStatusTransactionAbort : kBatchStatusError,
        response.failed_operation,
        diagnostic
    );
    if (!header) {
        return base::fail(std::move(header).error());
    }
    internal::append_big_endian(implementation.payload, std::uint16_t{0U});
    return emit_frame(
        implementation,
        sink,
        FrameTag::kBatchResult,
        base::Bytes{implementation.payload.data(), implementation.payload.size()}
    );
}

auto emit_terminal(
    ResponseEncoder::Impl& implementation,
    ResponseSink& sink,
    const TerminalResponseKind kind,
    const base::Error& error
) -> base::Status {
    const base::Bytes diagnostic = bounded_diagnostic(implementation, error);
    const FrameTag tag = kind == TerminalResponseKind::kTransactionAbort
                             ? FrameTag::kTransactionAbort
                             : FrameTag::kError;
    return emit_frame(implementation, sink, tag, diagnostic);
}

auto emit_response(
    ResponseEncoder::Impl& implementation,
    ResponseSink& sink,
    const RequestResponse& response
) -> base::Status {
    if (const auto* const stream = std::get_if<StreamResponse>(&response); stream != nullptr) {
        return emit_stream(implementation, sink, *stream);
    }
    if (const auto* const prepared = std::get_if<PrepareResponse>(&response); prepared != nullptr) {
        return emit_prepare(implementation, sink, *prepared);
    }
    if (const auto* const batch = std::get_if<BatchSuccessResponse>(&response); batch != nullptr) {
        return emit_batch_success(implementation, sink, *batch);
    }
    if (const auto* const batch = std::get_if<BatchFailureResponse>(&response); batch != nullptr) {
        return emit_batch_failure(implementation, sink, *batch);
    }
    if (const auto* const terminal = std::get_if<TerminalErrorResponse>(&response);
        terminal != nullptr) {
        return emit_terminal(implementation, sink, terminal->kind, terminal->error);
    }
    return base::fail(invariant_error("RMDB response variant was unknown", "emit response"));
}

}  // namespace

ResponseEncoder::ResponseEncoder() = default;

ResponseEncoder::ResponseEncoder(ResponseEncoder&&) noexcept = default;

auto ResponseEncoder::operator=(ResponseEncoder&&) noexcept -> ResponseEncoder& = default;

ResponseEncoder::~ResponseEncoder() = default;

auto ResponseEncoder::emit(const RequestResponse& response, ResponseSink& sink) -> base::Status {
    return base::invoke_exception_boundary(
        kEncoderExceptionBoundary,
        "emit response",
        [&]() -> base::Status {
            if (implementation_ == nullptr) {
                implementation_ = std::make_unique<Impl>();
            }
            return emit_response(*implementation_, sink, response);
        }
    );
}

auto ResponseEncoder::emit_terminal_error(
    const TerminalResponseKind kind,
    const base::Error& error,
    ResponseSink& sink
) -> base::Status {
    return base::invoke_exception_boundary(
        kEncoderExceptionBoundary,
        "emit terminal response",
        [&]() -> base::Status {
            if (implementation_ == nullptr) {
                implementation_ = std::make_unique<Impl>();
            }
            return emit_terminal(*implementation_, sink, kind, error);
        }
    );
}

auto ResponseEncoder::frame_buffer_capacity() const noexcept -> std::size_t {
    return implementation_ == nullptr ? 0U : implementation_->frame_encoder.buffer_capacity();
}

auto ResponseEncoder::payload_buffer_capacity() const noexcept -> std::size_t {
    return implementation_ == nullptr ? 0U : implementation_->payload.capacity();
}

}  // namespace db::protocol
