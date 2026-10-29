#include "db/protocol/rmdb_client.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
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

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/base/text.hpp"
#include "db/types/data_type.hpp"

#include "rmdb_client_request_codec.hpp"

namespace db::protocol {
namespace {

using SteadyClock = std::chrono::steady_clock;
using Deadline = SteadyClock::time_point;

inline constexpr std::size_t kClientReadBufferSize = std::size_t{32U} * std::size_t{1024U};
inline constexpr std::uint16_t kNoFailedBatchOperation = std::numeric_limits<std::uint16_t>::max();
inline constexpr base::ExceptionBoundaryPolicy kClientExceptionBoundary{
    .component = "protocol.rmdb_client",
    .allocation_message = "RMDB client could not allocate bounded protocol state",
    .exception_prefix = "unexpected exception escaped RMDB client: ",
    .unknown_exception_message = "unknown exception escaped RMDB client",
};

auto client_error(const base::ErrorCode code, std::string message, const std::string_view operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "protocol.rmdb_client",
        std::string{operation}
    );
}

auto peer_error(std::string message, const std::string_view operation) -> base::Error {
    return client_error(base::ErrorCode::kCorruption, std::move(message), operation);
}

auto input_error(std::string message, const std::string_view operation) -> base::Error {
    return client_error(base::ErrorCode::kInvalidArgument, std::move(message), operation);
}

auto deadline_after(const RmdbClientOptions& options) -> base::Result<Deadline> {
    if (options.io_timeout <= std::chrono::milliseconds::zero()) {
        return base::fail(
            input_error("RMDB client I/O timeout must be positive", "create deadline")
        );
    }
    if (options.maximum_response_bytes == 0U) {
        return base::fail(
            input_error("RMDB client response limit must be positive", "create deadline")
        );
    }
    return SteadyClock::now() + options.io_timeout;
}

auto remaining_timeout(const Deadline deadline, const std::string_view operation)
    -> base::Result<std::chrono::milliseconds> {
    const auto remaining = deadline - SteadyClock::now();
    if (remaining <= SteadyClock::duration::zero()) {
        return base::fail(client_error(
            base::ErrorCode::kUnavailable,
            "RMDB peer did not make progress before the operation deadline",
            operation
        ));
    }
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
    if (milliseconds <= std::chrono::milliseconds::zero()) {
        milliseconds = std::chrono::milliseconds{1};
    }
    return milliseconds;
}

auto wait_readable_until(
    const io::TcpStream& stream,
    const Deadline deadline,
    const std::string_view operation
) -> base::Status {
    auto timeout = remaining_timeout(deadline, operation);
    if (!timeout) {
        return base::fail(std::move(timeout).error());
    }
    auto ready = stream.wait_readable(timeout.value());
    if (!ready) {
        return base::fail(
            std::move(ready).error().with_context("protocol.rmdb_client", std::string{operation})
        );
    }
    if (ready.value() == io::TcpWaitResult::kTimedOut) {
        return base::fail(client_error(
            base::ErrorCode::kUnavailable,
            "RMDB peer did not make progress before the operation deadline",
            operation
        ));
    }
    return {};
}

auto wait_writable_until(
    const io::TcpStream& stream,
    const Deadline deadline,
    const std::string_view operation
) -> base::Status {
    auto timeout = remaining_timeout(deadline, operation);
    if (!timeout) {
        return base::fail(std::move(timeout).error());
    }
    auto ready = stream.wait_writable(timeout.value());
    if (!ready) {
        return base::fail(
            std::move(ready).error().with_context("protocol.rmdb_client", std::string{operation})
        );
    }
    if (ready.value() == io::TcpWaitResult::kTimedOut) {
        return base::fail(client_error(
            base::ErrorCode::kUnavailable,
            "RMDB peer did not make progress before the operation deadline",
            operation
        ));
    }
    return {};
}

auto write_until(
    const io::TcpStream& stream,
    base::Bytes bytes,
    const Deadline deadline,
    const std::string_view operation
) -> base::Status {
    while (!bytes.empty()) {
        auto writable = wait_writable_until(stream, deadline, operation);
        if (!writable) {
            return writable;
        }
        auto written = stream.try_write_some(bytes);
        if (!written) {
            if (written.error().code() == base::ErrorCode::kUnavailable) {
                continue;
            }
            return base::fail(
                std::move(written).error().with_context(
                    "protocol.rmdb_client",
                    std::string{operation}
                )
            );
        }
        if (written.value() == 0U) {
            return base::fail(
                client_error(base::ErrorCode::kIo, "RMDB socket write made no progress", operation)
            );
        }
        bytes = bytes.subspan(written.value());
    }
    return {};
}

auto read_exact_until(
    const io::TcpStream& stream,
    base::MutableBytes bytes,
    const Deadline deadline,
    const std::string_view operation
) -> base::Status {
    while (!bytes.empty()) {
        auto readable = wait_readable_until(stream, deadline, operation);
        if (!readable) {
            return readable;
        }
        auto received = stream.try_read_some(bytes);
        if (!received) {
            if (received.error().code() == base::ErrorCode::kUnavailable) {
                continue;
            }
            return base::fail(
                std::move(received).error().with_context(
                    "protocol.rmdb_client",
                    std::string{operation}
                )
            );
        }
        if (received.value() == 0U) {
            return base::fail(client_error(
                base::ErrorCode::kIo,
                "RMDB peer closed before completing an expected response",
                operation
            ));
        }
        bytes = bytes.subspan(received.value());
    }
    return {};
}

auto decode_wire_data_type(const std::uint8_t tag, const std::string_view operation)
    -> base::Result<WireDataType> {
    const auto decoded = wire_data_type_from_tag(tag);
    if (!decoded.has_value()) {
        return base::fail(peer_error("RMDB peer used an unknown SQL type tag", operation));
    }
    return decoded.value();
}

auto bytes_to_string(const base::Bytes bytes) -> std::string {
    std::string result;
    result.reserve(bytes.size());
    for (const std::byte byte : bytes) {
        result.push_back(static_cast<char>(std::to_integer<std::uint8_t>(byte)));
    }
    return result;
}

auto string_bytes(const std::string_view text) noexcept -> base::Bytes {
    return std::as_bytes(std::span{text.data(), text.size()});
}

auto decode_utf8(
    const base::Bytes bytes,
    const std::string_view field,
    const std::string_view operation
) -> base::Result<std::string> {
    std::string text = bytes_to_string(bytes);
    if (!base::is_valid_utf8(text)) {
        return base::fail(
            peer_error(std::string{"RMDB peer sent non-UTF-8 "}.append(field), operation)
        );
    }
    return text;
}

template <typename Value>
auto required_field(
    base::Result<Value> field,
    const std::string_view name,
    const std::string_view operation
) -> base::Result<Value> {
    if (!field) {
        return base::fail(peer_error(
            std::string{"RMDB peer response was truncated while reading "}.append(name),
            operation
        ));
    }
    return std::move(field).value();
}

auto decode_cell(
    base::BigEndianReader& reader,
    const WireDataType data_type,
    const std::string_view operation
) -> base::Result<types::Value> {
    auto marker = required_field(reader.read_u8(), "cell null marker", operation);
    if (!marker) {
        return base::fail(std::move(marker).error());
    }
    if (marker.value() == 0U) {
        return types::Value::null(wire_value_data_type(data_type));
    }
    if (marker.value() != 1U) {
        return base::fail(peer_error("RMDB peer used an invalid cell null marker", operation));
    }

    switch (data_type) {
        case WireDataType::kInt32: {
            auto value = required_field(reader.read_i32(), "INT32 cell", operation);
            if (!value) {
                return base::fail(std::move(value).error());
            }
            return types::Value::integer(static_cast<std::int64_t>(value.value()));
        }
        case WireDataType::kFloat32: {
            auto bits = required_field(reader.read_u32(), "FLOAT32 cell", operation);
            if (!bits) {
                return base::fail(std::move(bits).error());
            }
            static_assert(sizeof(float) == sizeof(std::uint32_t));
            return types::Value::floating(std::bit_cast<float>(bits.value()));
        }
        case WireDataType::kChar: {
            auto length = required_field(reader.read_u32(), "CHAR cell length", operation);
            if (!length) {
                return base::fail(std::move(length).error());
            }
            auto bytes = required_field(
                reader.read_bytes(static_cast<std::size_t>(length.value())),
                "CHAR cell",
                operation
            );
            if (!bytes) {
                return base::fail(std::move(bytes).error());
            }
            auto text = decode_utf8(bytes.value(), "CHAR cell", operation);
            if (!text) {
                return base::fail(std::move(text).error());
            }
            return types::Value::character(std::move(text).value());
        }
    }
    return base::fail(peer_error("RMDB peer selected an unknown cell type", operation));
}

auto decode_row(
    const base::Bytes payload,
    const std::span<const ColumnMetadata> columns,
    const std::string_view operation
) -> base::Result<std::vector<types::Value>> {
    base::BigEndianReader reader{payload};
    std::vector<types::Value> row;
    row.reserve(columns.size());
    for (const ColumnMetadata& column : columns) {
        auto value = decode_cell(reader, column.data_type, operation);
        if (!value) {
            return base::fail(std::move(value).error());
        }
        row.push_back(std::move(value).value());
    }
    auto finished = reader.finish();
    if (!finished) {
        return base::fail(peer_error("RMDB ROW payload had trailing bytes", operation));
    }
    return row;
}

auto decode_columns(
    base::BigEndianReader& reader,
    const std::size_t count,
    const std::string_view operation
) -> base::Result<std::vector<ColumnMetadata>> {
    std::vector<ColumnMetadata> columns;
    columns.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        auto name_length = required_field(reader.read_u16(), "column name length", operation);
        if (!name_length) {
            return base::fail(std::move(name_length).error());
        }
        if (name_length.value() == 0U) {
            return base::fail(peer_error("RMDB peer returned an empty column name", operation));
        }
        auto name_bytes = required_field(
            reader.read_bytes(static_cast<std::size_t>(name_length.value())),
            "column name",
            operation
        );
        if (!name_bytes) {
            return base::fail(std::move(name_bytes).error());
        }
        auto name = decode_utf8(name_bytes.value(), "column name", operation);
        if (!name) {
            return base::fail(std::move(name).error());
        }
        auto type_tag = required_field(reader.read_u8(), "column SQL type", operation);
        if (!type_tag) {
            return base::fail(std::move(type_tag).error());
        }
        auto data_type = decode_wire_data_type(type_tag.value(), operation);
        if (!data_type) {
            return base::fail(std::move(data_type).error());
        }
        columns.push_back(
            ColumnMetadata{
                .name = std::move(name).value(),
                .data_type = data_type.value(),
            }
        );
    }
    return columns;
}

auto decode_meta(const base::Bytes payload, const std::string_view operation)
    -> base::Result<std::vector<ColumnMetadata>> {
    base::BigEndianReader reader{payload};
    auto count = required_field(reader.read_u16(), "META column count", operation);
    if (!count) {
        return base::fail(std::move(count).error());
    }
    auto columns = decode_columns(reader, count.value(), operation);
    if (!columns) {
        return base::fail(std::move(columns).error());
    }
    auto finished = reader.finish();
    if (!finished) {
        return base::fail(peer_error("RMDB META payload had trailing bytes", operation));
    }
    return columns;
}

auto decode_u64_payload(const base::Bytes payload, const std::string_view frame_name)
    -> base::Result<std::uint64_t> {
    if (payload.size() != sizeof(std::uint64_t)) {
        return base::fail(peer_error(
            std::string{"RMDB "}.append(frame_name).append(" payload had the wrong length"),
            "decode terminal"
        ));
    }
    base::BigEndianReader reader{payload};
    auto value = required_field(reader.read_u64(), "terminal row count", "decode terminal");
    if (!value) {
        return base::fail(std::move(value).error());
    }
    return value.value();
}

auto decode_diagnostic(const base::Bytes payload, const std::string_view frame_name)
    -> base::Result<std::string> {
    if (payload.size() > kMaximumRmdbDiagnosticSize) {
        return base::fail(peer_error(
            std::string{"RMDB "}.append(frame_name).append(" diagnostic exceeded 64 KiB"),
            "decode diagnostic"
        ));
    }
    return decode_utf8(payload, "diagnostic", "decode diagnostic");
}

auto decode_prepare_ok_entries(
    const base::Bytes payload,
    const std::span<const RmdbPreparedStatement> statements
) -> base::Result<std::vector<RmdbPreparedEntry>> {
    base::BigEndianReader reader{payload};
    auto count =
        required_field(reader.read_u16(), "PREPARE_OK statement count", "decode PREPARE_OK");
    if (!count) {
        return base::fail(std::move(count).error());
    }
    if (count.value() != statements.size()) {
        return base::fail(peer_error(
            "PREPARE_OK statement count did not match the submitted dictionary",
            "decode PREPARE_OK"
        ));
    }

    std::vector<RmdbPreparedEntry> entries;
    entries.reserve(statements.size());
    for (const RmdbPreparedStatement& expected : statements) {
        auto statement_id =
            required_field(reader.read_u16(), "PREPARE_OK statement id", "decode PREPARE_OK");
        if (!statement_id) {
            return base::fail(std::move(statement_id).error());
        }
        if (statement_id.value() != expected.statement_id) {
            return base::fail(peer_error(
                "PREPARE_OK statement order or ID did not match the submitted dictionary",
                "decode PREPARE_OK"
            ));
        }
        auto column_count =
            required_field(reader.read_u16(), "PREPARE_OK column count", "decode PREPARE_OK");
        if (!column_count) {
            return base::fail(std::move(column_count).error());
        }
        auto columns = decode_columns(reader, column_count.value(), "decode PREPARE_OK");
        if (!columns) {
            return base::fail(std::move(columns).error());
        }
        const bool is_query = !columns->empty();
        if ((expected.result_kind == RmdbPreparedResultKind::kQuery) != is_query) {
            return base::fail(peer_error(
                "PREPARE_OK result kind did not match the declared statement shape",
                "decode PREPARE_OK"
            ));
        }
        if (columns->size() != expected.result_types.size()) {
            return base::fail(peer_error(
                "PREPARE_OK result column count did not match the client declaration",
                "decode PREPARE_OK"
            ));
        }
        for (std::size_t column_index = 0U; column_index < columns->size(); ++column_index) {
            if ((*columns)[column_index].data_type != expected.result_types[column_index]) {
                return base::fail(peer_error(
                    "PREPARE_OK result types did not match the client declaration",
                    "decode PREPARE_OK"
                ));
            }
        }
        entries.push_back(
            RmdbPreparedEntry{
                .statement_id = expected.statement_id,
                .parameter_types = expected.parameter_types,
                .result_kind = expected.result_kind,
                .columns = std::move(columns).value(),
            }
        );
    }
    auto finished = reader.finish();
    if (!finished) {
        return base::fail(peer_error("PREPARE_OK payload had trailing bytes", "decode PREPARE_OK"));
    }
    return entries;
}

struct BatchResultHeader final {
    std::uint16_t executed_count{0U};
    std::uint8_t status{0U};
    std::uint16_t failed_operation{kNoFailedBatchOperation};
    std::string diagnostic;
    std::uint16_t result_count{0U};
};

auto decode_batch_result_header(base::BigEndianReader& reader, const std::string_view operation)
    -> base::Result<BatchResultHeader> {
    auto executed_count = required_field(reader.read_u16(), "executed operation count", operation);
    if (!executed_count) {
        return base::fail(std::move(executed_count).error());
    }
    auto status = required_field(reader.read_u8(), "batch status", operation);
    if (!status) {
        return base::fail(std::move(status).error());
    }
    auto failed_operation = required_field(reader.read_u16(), "failed operation", operation);
    if (!failed_operation) {
        return base::fail(std::move(failed_operation).error());
    }
    auto diagnostic_length = required_field(reader.read_u32(), "diagnostic length", operation);
    if (!diagnostic_length) {
        return base::fail(std::move(diagnostic_length).error());
    }
    if (diagnostic_length.value() > kMaximumRmdbDiagnosticSize) {
        return base::fail(peer_error("BATCH_RESULT diagnostic exceeded 64 KiB", operation));
    }
    auto diagnostic_bytes = required_field(
        reader.read_bytes(static_cast<std::size_t>(diagnostic_length.value())),
        "diagnostic",
        operation
    );
    if (!diagnostic_bytes) {
        return base::fail(std::move(diagnostic_bytes).error());
    }
    auto diagnostic = decode_diagnostic(diagnostic_bytes.value(), "BATCH_RESULT");
    if (!diagnostic) {
        return base::fail(std::move(diagnostic).error());
    }
    auto result_count = required_field(reader.read_u16(), "result count", operation);
    if (!result_count) {
        return base::fail(std::move(result_count).error());
    }
    return BatchResultHeader{
        .executed_count = executed_count.value(),
        .status = status.value(),
        .failed_operation = failed_operation.value(),
        .diagnostic = std::move(diagnostic).value(),
        .result_count = result_count.value(),
    };
}

auto decode_batch_query_rows(
    base::BigEndianReader& reader,
    const std::vector<ColumnMetadata>& columns,
    const std::uint32_t row_count,
    const std::string_view operation
) -> base::Result<std::vector<std::vector<types::Value>>> {
    if (columns.empty()
        || static_cast<std::size_t>(row_count) > reader.remaining() / columns.size()) {
        return base::fail(
            peer_error("BATCH_RESULT row count exceeded the remaining payload", operation)
        );
    }
    std::vector<std::vector<types::Value>> rows;
    rows.reserve(row_count);
    for (std::size_t row_index = 0U; row_index < row_count; ++row_index) {
        std::vector<types::Value> row;
        row.reserve(columns.size());
        for (const ColumnMetadata& column : columns) {
            auto value = decode_cell(reader, column.data_type, operation);
            if (!value) {
                return base::fail(std::move(value).error());
            }
            row.push_back(std::move(value).value());
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

auto decode_batch_result_set(
    base::BigEndianReader& reader,
    const RmdbPreparedCatalog& catalog,
    const std::span<const RmdbBatchOperation> operations,
    RmdbBatchResponse& result,
    const std::optional<std::size_t> previous_index,
    const std::string_view operation
) -> base::Result<std::size_t> {
    auto operation_index = required_field(reader.read_u16(), "result operation index", operation);
    if (!operation_index) {
        return base::fail(std::move(operation_index).error());
    }
    const std::size_t index = operation_index.value();
    if (index >= operations.size()
        || (previous_index.has_value() && index <= previous_index.value())) {
        return base::fail(peer_error(
            "BATCH_RESULT result operation indices were unordered or out of range",
            operation
        ));
    }
    const RmdbPreparedEntry* const entry = catalog.find(operations[index].statement_id);
    if (entry == nullptr || entry->result_kind != RmdbPreparedResultKind::kQuery) {
        return base::fail(
            peer_error("BATCH_RESULT attached rows to a non-query prepared statement", operation)
        );
    }
    auto row_count = required_field(reader.read_u32(), "query row count", operation);
    if (!row_count) {
        return base::fail(std::move(row_count).error());
    }
    auto rows = decode_batch_query_rows(reader, entry->columns, row_count.value(), operation);
    if (!rows) {
        return base::fail(std::move(rows).error());
    }
    result.responses[index] = RmdbResponse{
        .columns = entry->columns,
        .rows = std::move(rows).value(),
        .terminal = RmdbTerminal{
            .kind = RmdbTerminalKind::kResultEnd,
            .row_count = row_count.value(),
            .diagnostic = {},
        },
    };
    return index;
}

auto validate_successful_batch_result(
    const BatchResultHeader& header,
    const RmdbPreparedCatalog& catalog,
    const std::span<const RmdbBatchOperation> operations,
    const RmdbBatchResponse& result,
    const std::string_view operation
) -> base::Status {
    std::size_t expected_result_count{0U};
    for (std::size_t index = 0U; index < operations.size(); ++index) {
        const RmdbPreparedEntry* const entry = catalog.find(operations[index].statement_id);
        if (entry == nullptr) {
            return base::fail(
                input_error("EXEC_BATCH used a catalog inconsistent with its operations", operation)
            );
        }
        const bool expects_result = entry->result_kind == RmdbPreparedResultKind::kQuery;
        if (expects_result) {
            ++expected_result_count;
        }
        if (result.responses[index].has_value() != expects_result) {
            return base::fail(
                peer_error("successful BATCH_RESULT omitted or invented a query result", operation)
            );
        }
    }
    if (header.executed_count != operations.size()
        || header.failed_operation != kNoFailedBatchOperation || !header.diagnostic.empty()
        || header.result_count != expected_result_count) {
        return base::fail(
            peer_error("successful BATCH_RESULT metadata was inconsistent", operation)
        );
    }
    return {};
}

auto validate_failed_batch_result(
    const BatchResultHeader& header,
    const std::span<const RmdbBatchOperation> operations,
    const std::string_view operation
) -> base::Status {
    if (header.executed_count >= operations.size()
        || header.failed_operation != header.executed_count || header.result_count != 0U) {
        return base::fail(peer_error("failed BATCH_RESULT metadata was inconsistent", operation));
    }
    return {};
}

auto decode_batch_result(
    const base::Bytes payload,
    const RmdbPreparedCatalog& catalog,
    const std::span<const RmdbBatchOperation> operations
) -> base::Result<RmdbBatchResponse> {
    constexpr std::string_view k_operation = "decode BATCH_RESULT";
    base::BigEndianReader reader{payload};
    auto header = decode_batch_result_header(reader, k_operation);
    if (!header) {
        return base::fail(std::move(header).error());
    }
    if (header->result_count > operations.size()) {
        return base::fail(peer_error("BATCH_RESULT contained too many result sets", k_operation));
    }

    RmdbBatchResponse result;
    result.executed_operation_count = header->executed_count;
    result.responses.resize(operations.size());
    std::optional<std::size_t> previous_index;
    for (std::size_t result_index = 0U; result_index < header->result_count; ++result_index) {
        auto index = decode_batch_result_set(
            reader,
            catalog,
            operations,
            result,
            previous_index,
            k_operation
        );
        if (!index) {
            return base::fail(std::move(index).error());
        }
        previous_index = index.value();
    }
    auto finished = reader.finish();
    if (!finished) {
        return base::fail(peer_error("BATCH_RESULT payload had trailing bytes", k_operation));
    }

    switch (header->status) {
        case 0U: {
            auto valid = validate_successful_batch_result(
                header.value(),
                catalog,
                operations,
                result,
                k_operation
            );
            if (!valid) {
                return base::fail(std::move(valid).error());
            }
            result.terminal = RmdbBatchTerminal{
                .kind = RmdbBatchTerminalKind::kOk,
                .failed_operation = 0U,
                .diagnostic = {},
            };
            return result;
        }
        case 1U:
        case 2U: {
            auto valid = validate_failed_batch_result(header.value(), operations, k_operation);
            if (!valid) {
                return base::fail(std::move(valid).error());
            }
            std::fill(result.responses.begin(), result.responses.end(), std::nullopt);
            result.terminal = RmdbBatchTerminal{
                .kind = header->status == 1U ? RmdbBatchTerminalKind::kTransactionAbort
                                             : RmdbBatchTerminalKind::kError,
                .failed_operation = header->failed_operation,
                .diagnostic = std::move(header->diagnostic),
            };
            return result;
        }
        default:
            return base::fail(peer_error("BATCH_RESULT used an unknown status", k_operation));
    }
}

}  // namespace

struct RmdbClient::Impl final {
    Impl(
        io::TcpStream stream_value,
        const RmdbClientOptions options_value,
        std::shared_ptr<const std::byte> connection_token_value
    )
        : stream{std::move(stream_value)},
          options{options_value},
          connection_token{std::move(connection_token_value)} {}

    io::TcpStream stream;
    RmdbClientOptions options;
    std::shared_ptr<const std::byte> connection_token;
    FrameEncoder outbound_encoder;
    FrameDecoder inbound_decoder;
    std::vector<std::byte> buffered_input;
};

namespace {

struct ReceivedFrame final {
    FrameTag tag{FrameTag::kError};
    std::vector<std::byte> payload;
};

template <typename Implementation>
auto consume_buffered_response_frame(Implementation& implementation, std::size_t& observed_payload)
    -> base::Result<std::optional<ReceivedFrame>> {
    if (implementation.buffered_input.empty()) {
        return std::optional<ReceivedFrame>{};
    }
    auto decoded = implementation.inbound_decoder.consume(
        base::Bytes{
            implementation.buffered_input.data(),
            implementation.buffered_input.size(),
        }
    );
    if (!decoded) {
        return base::fail(
            std::move(decoded).error().with_context("protocol.rmdb_client", "decode response frame")
        );
    }
    if (decoded->consumed == 0U) {
        return base::fail(client_error(
            base::ErrorCode::kInvariantViolation,
            "RMDB frame decoder made no progress",
            "decode response frame"
        ));
    }
    implementation.buffered_input.erase(
        implementation.buffered_input.begin(),
        implementation.buffered_input.begin() + static_cast<std::ptrdiff_t>(decoded->consumed)
    );
    if (!decoded->frame.has_value()) {
        return std::optional<ReceivedFrame>{};
    }
    auto envelope =
        validate_inbound_frame_envelope(FrameDirection::kServerToClient, *decoded->frame);
    if (!envelope) {
        return base::fail(
            std::move(envelope).error().with_context(
                "protocol.rmdb_client",
                "validate response frame"
            )
        );
    }
    if (decoded->frame->payload.size() > implementation.options.maximum_response_bytes
        || observed_payload
               > implementation.options.maximum_response_bytes - decoded->frame->payload.size()) {
        return base::fail(client_error(
            base::ErrorCode::kResourceExhausted,
            "RMDB response exceeded the configured client limit",
            "read response frame"
        ));
    }
    observed_payload += decoded->frame->payload.size();
    return std::optional<ReceivedFrame>{ReceivedFrame{
        .tag = envelope->tag,
        .payload = std::vector<std::byte>{
            decoded->frame->payload.begin(),
            decoded->frame->payload.end(),
        },
    }};
}

template <typename Implementation>
auto read_response_frame(
    Implementation& implementation,
    const Deadline deadline,
    std::size_t& observed_payload
) -> base::Result<ReceivedFrame> {
    std::array<std::byte, kClientReadBufferSize> input{};
    while (true) {
        auto buffered = consume_buffered_response_frame(implementation, observed_payload);
        if (!buffered) {
            return base::fail(std::move(buffered).error());
        }
        if (buffered->has_value()) {
            return std::move(buffered).value().value();
        }

        auto readable = wait_readable_until(implementation.stream, deadline, "read response frame");
        if (!readable) {
            return base::fail(std::move(readable).error());
        }
        auto received = implementation.stream.try_read_some(input);
        if (!received) {
            if (received.error().code() == base::ErrorCode::kUnavailable) {
                continue;
            }
            return base::fail(
                std::move(received).error().with_context(
                    "protocol.rmdb_client",
                    "read response frame"
                )
            );
        }
        if (received.value() == 0U) {
            return base::fail(client_error(
                base::ErrorCode::kIo,
                "RMDB peer closed before a complete response frame",
                "read response frame"
            ));
        }
        const auto received_input = std::span{input}.first(received.value());
        implementation.buffered_input.insert(
            implementation.buffered_input.end(),
            received_input.begin(),
            received_input.end()
        );
    }
}

struct ExecuteAccumulator final {
    std::optional<std::vector<ColumnMetadata>> columns;
    std::vector<std::vector<types::Value>> rows;
};

auto decode_meta_frame(
    const base::Bytes payload,
    ExecuteAccumulator& accumulator,
    const std::string_view operation
) -> base::Result<std::optional<RmdbResponse>> {
    if (accumulator.columns.has_value() || !accumulator.rows.empty()) {
        return base::fail(
            peer_error("RMDB response sent duplicate or out-of-order META", operation)
        );
    }
    auto columns = decode_meta(payload, "decode META");
    if (!columns) {
        return base::fail(std::move(columns).error());
    }
    if (columns->empty()) {
        return base::fail(peer_error("RMDB response META declared zero columns", operation));
    }
    accumulator.columns.emplace(std::move(columns).value());
    return std::optional<RmdbResponse>{};
}

auto decode_row_frame(
    const base::Bytes payload,
    ExecuteAccumulator& accumulator,
    const std::string_view operation
) -> base::Result<std::optional<RmdbResponse>> {
    if (!accumulator.columns.has_value()) {
        return base::fail(peer_error("RMDB response ROW arrived before META", operation));
    }
    auto row = decode_row(payload, *accumulator.columns, "decode ROW");
    if (!row) {
        return base::fail(std::move(row).error());
    }
    accumulator.rows.push_back(std::move(row).value());
    return std::optional<RmdbResponse>{};
}

auto decode_command_ok_frame(
    const ReceivedFrame& frame,
    const ExecuteAccumulator& accumulator,
    const std::string_view operation
) -> base::Result<std::optional<RmdbResponse>> {
    if (!frame.payload.empty() || accumulator.columns.has_value() || !accumulator.rows.empty()) {
        return base::fail(
            peer_error("RMDB COMMAND_OK did not terminate an empty command response", operation)
        );
    }
    return std::optional<RmdbResponse>{RmdbResponse{
        .columns = {},
        .rows = {},
        .terminal = RmdbTerminal{
            .kind = RmdbTerminalKind::kCommandOk,
            .row_count = 0U,
            .diagnostic = {},
        },
    }};
}

auto decode_result_end_frame(
    const base::Bytes payload,
    ExecuteAccumulator& accumulator,
    const std::string_view operation
) -> base::Result<std::optional<RmdbResponse>> {
    if (!accumulator.columns.has_value()) {
        return base::fail(peer_error("RMDB RESULT_END arrived without META", operation));
    }
    auto row_count = decode_u64_payload(payload, "RESULT_END");
    if (!row_count) {
        return base::fail(std::move(row_count).error());
    }
    if (row_count.value() != accumulator.rows.size()) {
        return base::fail(
            peer_error("RMDB RESULT_END row count did not match received ROW frames", operation)
        );
    }
    return std::optional<RmdbResponse>{RmdbResponse{
        .columns = std::move(accumulator.columns).value(),
        .rows = std::move(accumulator.rows),
        .terminal = RmdbTerminal{
            .kind = RmdbTerminalKind::kResultEnd,
            .row_count = row_count.value(),
            .diagnostic = {},
        },
    }};
}

auto decode_terminal_frame(const ReceivedFrame& frame, const base::Bytes payload)
    -> base::Result<std::optional<RmdbResponse>> {
    auto diagnostic = decode_diagnostic(
        payload,
        frame.tag == FrameTag::kTransactionAbort ? "TRANSACTION_ABORT" : "ERROR"
    );
    if (!diagnostic) {
        return base::fail(std::move(diagnostic).error());
    }
    return std::optional<RmdbResponse>{RmdbResponse{
        .columns = {},
        .rows = {},
        .terminal = RmdbTerminal{
            .kind = frame.tag == FrameTag::kTransactionAbort ? RmdbTerminalKind::kTransactionAbort
                                                             : RmdbTerminalKind::kError,
            .diagnostic = std::move(diagnostic).value(),
        },
    }};
}

auto decode_execute_frame(const ReceivedFrame& frame, ExecuteAccumulator& accumulator)
    -> base::Result<std::optional<RmdbResponse>> {
    constexpr std::string_view k_operation = "decode EXEC_STREAM";
    const base::Bytes payload{frame.payload.data(), frame.payload.size()};
    switch (frame.tag) {
        case FrameTag::kMeta:
            return decode_meta_frame(payload, accumulator, k_operation);
        case FrameTag::kRow:
            return decode_row_frame(payload, accumulator, k_operation);
        case FrameTag::kCommandOk:
            return decode_command_ok_frame(frame, accumulator, k_operation);
        case FrameTag::kResultEnd:
            return decode_result_end_frame(payload, accumulator, k_operation);
        case FrameTag::kTransactionAbort:
        case FrameTag::kError:
            return decode_terminal_frame(frame, payload);
        case FrameTag::kPrepareOk:
        case FrameTag::kBatchResult:
        case FrameTag::kExecStream:
        case FrameTag::kPrepareSet:
        case FrameTag::kExecBatch:
            return base::fail(
                peer_error("RMDB EXEC_STREAM received an unexpected response frame", k_operation)
            );
    }
    return base::fail(peer_error("RMDB EXEC_STREAM used an unknown response frame", k_operation));
}

template <typename Implementation>
auto send_request_frame(
    Implementation& implementation,
    const FrameTag tag,
    const std::uint8_t flags,
    const base::Bytes payload,
    const Deadline deadline
) -> base::Status {
    auto encoded = implementation.outbound_encoder
                       .encode(FrameDirection::kClientToServer, tag, flags, payload);
    if (!encoded) {
        return base::fail(
            std::move(encoded).error().with_context("protocol.rmdb_client", "encode request frame")
        );
    }
    return write_until(implementation.stream, encoded.value(), deadline, "write request frame");
}

}  // namespace

auto RmdbPreparedStatement::command(
    const std::uint16_t statement_id,
    std::string sql,
    std::vector<WireDataType> parameter_types
) -> RmdbPreparedStatement {
    return RmdbPreparedStatement{
        .statement_id = statement_id,
        .sql = std::move(sql),
        .parameter_types = std::move(parameter_types),
        .result_kind = RmdbPreparedResultKind::kCommand,
        .result_types = {},
    };
}

auto RmdbPreparedStatement::query(
    const std::uint16_t statement_id,
    std::string sql,
    std::vector<WireDataType> parameter_types,
    std::vector<WireDataType> result_types
) -> RmdbPreparedStatement {
    return RmdbPreparedStatement{
        .statement_id = statement_id,
        .sql = std::move(sql),
        .parameter_types = std::move(parameter_types),
        .result_kind = RmdbPreparedResultKind::kQuery,
        .result_types = std::move(result_types),
    };
}

auto RmdbPreparedCatalog::contains(const std::uint16_t statement_id) const noexcept -> bool {
    return find(statement_id) != nullptr;
}

auto RmdbPreparedCatalog::find(const std::uint16_t statement_id) const noexcept
    -> const RmdbPreparedEntry* {
    const auto found = std::find_if(
        entries_.begin(),
        entries_.end(),
        [statement_id](const RmdbPreparedEntry& entry) {
            return entry.statement_id == statement_id;
        }
    );
    return found == entries_.end() ? nullptr : std::addressof(*found);
}

auto RmdbPreparedCatalog::entries() const noexcept -> std::span<const RmdbPreparedEntry> {
    return entries_;
}

auto RmdbTerminal::is_success() const noexcept -> bool {
    return kind == RmdbTerminalKind::kCommandOk || kind == RmdbTerminalKind::kResultEnd;
}

auto RmdbTerminal::is_transaction_abort() const noexcept -> bool {
    return kind == RmdbTerminalKind::kTransactionAbort;
}

auto RmdbBatchTerminal::is_success() const noexcept -> bool {
    return kind == RmdbBatchTerminalKind::kOk;
}

auto RmdbBatchTerminal::is_transaction_abort() const noexcept -> bool {
    return kind == RmdbBatchTerminalKind::kTransactionAbort;
}

RmdbClient::RmdbClient(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

auto RmdbClient::connect(const io::TcpEndpoint& endpoint, const RmdbClientOptions options)
    -> base::Result<RmdbClient> {
    return base::invoke_exception_boundary(
        kClientExceptionBoundary,
        "connect",
        [&]() -> base::Result<RmdbClient> {
            auto deadline = deadline_after(options);
            if (!deadline) {
                return base::fail(std::move(deadline).error());
            }
            auto stream = io::TcpStream::connect(endpoint);
            if (!stream) {
                return base::fail(
                    std::move(stream).error().with_context("protocol.rmdb_client", "connect")
                );
            }
            auto no_delay = stream->set_no_delay();
            if (!no_delay) {
                return base::fail(
                    std::move(no_delay).error().with_context(
                        "protocol.rmdb_client",
                        "enable TCP_NODELAY"
                    )
                );
            }
            auto implementation = std::make_unique<Impl>(
                std::move(stream).value(),
                options,
                std::make_shared<std::byte>(std::byte{0U})
            );
            auto sent = write_until(
                implementation->stream,
                base::Bytes{kRmdbV3Preface.data(), kRmdbV3Preface.size()},
                deadline.value(),
                "write RMDB preface"
            );
            if (!sent) {
                return base::fail(std::move(sent).error());
            }
            std::array<std::byte, kRmdbV3PrefaceSize> echoed{};
            auto received = read_exact_until(
                implementation->stream,
                echoed,
                deadline.value(),
                "read RMDB preface"
            );
            if (!received) {
                return base::fail(std::move(received).error());
            }
            auto valid = validate_rmdb_v3_preface(echoed);
            if (!valid) {
                return base::fail(
                    std::move(valid).error().with_context(
                        "protocol.rmdb_client",
                        "validate RMDB preface"
                    )
                );
            }
            return RmdbClient{std::move(implementation)};
        }
    );
}

RmdbClient::RmdbClient(RmdbClient&& other) noexcept = default;

auto RmdbClient::operator=(RmdbClient&& other) noexcept -> RmdbClient& {
    if (this != &other) {
        try {
            static_cast<void>(close());
            // Move assignment is a noexcept ownership boundary; explicit close reports failures.
        } catch (...) {  // NOLINT(bugprone-empty-catch)
        }
        implementation_ = std::move(other.implementation_);
    }
    return *this;
}

RmdbClient::~RmdbClient() {
    try {
        static_cast<void>(close());
        // A destructor performs best-effort protocol shutdown and cannot propagate exceptions.
    } catch (...) {  // NOLINT(bugprone-empty-catch)
    }
}

auto RmdbClient::is_open() const noexcept -> bool {
    return implementation_ != nullptr && implementation_->stream.is_open();
}

auto RmdbClient::close() -> base::Status {
    if (implementation_ == nullptr) {
        return {};
    }
    auto closed = implementation_->stream.close();
    implementation_.reset();
    if (!closed) {
        return base::fail(std::move(closed).error().with_context("protocol.rmdb_client", "close"));
    }
    return {};
}

auto RmdbClient::execute(const std::string_view sql) -> base::Result<RmdbResponse> {
    return base::invoke_exception_boundary(
        kClientExceptionBoundary,
        "execute EXEC_STREAM",
        [&]() -> base::Result<RmdbResponse> {
            if (!is_open()) {
                return base::fail(client_error(
                    base::ErrorCode::kUnavailable,
                    "RMDB client is not connected",
                    "execute EXEC_STREAM"
                ));
            }
            auto valid_sql = internal::validate_rmdb_sql_request(sql, "execute EXEC_STREAM");
            if (!valid_sql) {
                return base::fail(std::move(valid_sql).error());
            }
            auto deadline = deadline_after(implementation_->options);
            if (!deadline) {
                return base::fail(std::move(deadline).error());
            }
            const base::Bytes payload = string_bytes(sql);
            auto sent = send_request_frame(
                *implementation_,
                FrameTag::kExecStream,
                kRmdbNoFrameFlags,
                payload,
                deadline.value()
            );
            if (!sent) {
                return base::fail(std::move(sent).error());
            }

            ExecuteAccumulator accumulator;
            std::size_t observed_payload{0U};
            while (true) {
                auto frame =
                    read_response_frame(*implementation_, deadline.value(), observed_payload);
                if (!frame) {
                    return base::fail(std::move(frame).error());
                }
                auto response = decode_execute_frame(*frame, accumulator);
                if (!response) {
                    return base::fail(std::move(response).error());
                }
                if (response->has_value()) {
                    return std::move(response).value().value();
                }
            }
        }
    );
}

auto RmdbClient::prepare_set(const std::span<const RmdbPreparedStatement> statements)
    -> base::Result<RmdbPreparedCatalog> {
    return base::invoke_exception_boundary(
        kClientExceptionBoundary,
        "execute PREPARE_SET",
        [&]() -> base::Result<RmdbPreparedCatalog> {
            if (!is_open()) {
                return base::fail(client_error(
                    base::ErrorCode::kUnavailable,
                    "RMDB client is not connected",
                    "execute PREPARE_SET"
                ));
            }
            auto valid = internal::validate_rmdb_prepared_statements(statements);
            if (!valid) {
                return base::fail(std::move(valid).error());
            }
            auto payload = internal::encode_rmdb_prepare_set(statements);
            if (!payload) {
                return base::fail(std::move(payload).error());
            }
            auto deadline = deadline_after(implementation_->options);
            if (!deadline) {
                return base::fail(std::move(deadline).error());
            }
            auto sent = send_request_frame(
                *implementation_,
                FrameTag::kPrepareSet,
                kRmdbNoFrameFlags,
                base::Bytes{payload->data(), payload->size()},
                deadline.value()
            );
            if (!sent) {
                return base::fail(std::move(sent).error());
            }
            std::size_t observed_payload{0U};
            auto frame = read_response_frame(*implementation_, deadline.value(), observed_payload);
            if (!frame) {
                return base::fail(std::move(frame).error());
            }
            const base::Bytes response_payload{frame->payload.data(), frame->payload.size()};
            if (frame->tag == FrameTag::kPrepareOk) {
                auto entries = decode_prepare_ok_entries(response_payload, statements);
                if (!entries) {
                    return base::fail(std::move(entries).error());
                }
                RmdbPreparedCatalog catalog;
                catalog.entries_ = std::move(entries).value();
                catalog.connection_token_ = implementation_->connection_token;
                return catalog;
            }
            if (frame->tag == FrameTag::kError) {
                auto diagnostic = decode_diagnostic(response_payload, "ERROR");
                if (!diagnostic) {
                    return base::fail(std::move(diagnostic).error());
                }
                return base::fail(client_error(
                    base::ErrorCode::kExecution,
                    std::string{"RMDB server rejected PREPARE_SET: "}.append(diagnostic.value()),
                    "execute PREPARE_SET"
                ));
            }
            return base::fail(peer_error(
                "RMDB PREPARE_SET received an unexpected response frame",
                "execute PREPARE_SET"
            ));
        }
    );
}

auto RmdbClient::execute_batch(
    const RmdbPreparedCatalog& catalog,
    const std::span<const RmdbBatchOperation> operations
) -> base::Result<RmdbBatchResponse> {
    return base::invoke_exception_boundary(
        kClientExceptionBoundary,
        "execute EXEC_BATCH",
        [&]() -> base::Result<RmdbBatchResponse> {
            if (!is_open()) {
                return base::fail(client_error(
                    base::ErrorCode::kUnavailable,
                    "RMDB client is not connected",
                    "execute EXEC_BATCH"
                ));
            }
            if (catalog.connection_token_ == nullptr
                || catalog.connection_token_.get() != implementation_->connection_token.get()) {
                return base::fail(input_error(
                    "EXEC_BATCH catalog did not originate from this RMDB client connection",
                    "execute EXEC_BATCH"
                ));
            }
            auto payload = internal::encode_rmdb_batch_request(catalog, operations);
            if (!payload) {
                return base::fail(std::move(payload).error());
            }
            auto deadline = deadline_after(implementation_->options);
            if (!deadline) {
                return base::fail(std::move(deadline).error());
            }
            auto sent = send_request_frame(
                *implementation_,
                FrameTag::kExecBatch,
                kRmdbAutoAbortFrameFlag,
                base::Bytes{payload->data(), payload->size()},
                deadline.value()
            );
            if (!sent) {
                return base::fail(std::move(sent).error());
            }
            std::size_t observed_payload{0U};
            auto frame = read_response_frame(*implementation_, deadline.value(), observed_payload);
            if (!frame) {
                return base::fail(std::move(frame).error());
            }
            const base::Bytes response_payload{frame->payload.data(), frame->payload.size()};
            if (frame->tag == FrameTag::kBatchResult) {
                return decode_batch_result(response_payload, catalog, operations);
            }
            if (frame->tag == FrameTag::kError) {
                auto diagnostic = decode_diagnostic(response_payload, "ERROR");
                if (!diagnostic) {
                    return base::fail(std::move(diagnostic).error());
                }
                RmdbBatchResponse response;
                response.responses.resize(operations.size());
                response.terminal = RmdbBatchTerminal{
                    .kind = RmdbBatchTerminalKind::kError,
                    .failed_operation = 0U,
                    .diagnostic = std::move(diagnostic).value(),
                };
                return response;
            }
            return base::fail(peer_error(
                "RMDB EXEC_BATCH received an unexpected response frame",
                "execute EXEC_BATCH"
            ));
        }
    );
}

}  // namespace db::protocol
