#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/result.hpp"
#include "db/protocol/frame_codec.hpp"
#include "db/protocol/session_port.hpp"

namespace db::protocol {

// Metadata returned for each PREPARE_SET definition in request order. The
// response encoder serializes it as PREPARE_OK without querying the port again.
struct PreparedStatementMetadata final {
    std::uint16_t statement_id{0U};
    std::vector<ColumnMetadata> columns;

    auto operator==(const PreparedStatementMetadata&) const -> bool = default;
};

// A completed EXEC_STREAM command or query before it is serialized into
// COMMAND_OK or META/ROW/RESULT_END frames.
struct StreamResponse final {
    ExecutionResult execution;

    auto operator==(const StreamResponse&) const -> bool = default;
};

struct PrepareResponse final {
    std::vector<PreparedStatementMetadata> statements;

    auto operator==(const PrepareResponse&) const -> bool = default;
};

struct BatchQueryResult final {
    std::uint16_t operation_index{0U};
    ExecutionResult execution;

    auto operator==(const BatchQueryResult&) const -> bool = default;
};

struct BatchSuccessResponse final {
    std::uint16_t executed_operation_count{0U};
    std::vector<BatchQueryResult> query_results;

    auto operator==(const BatchSuccessResponse&) const -> bool = default;
};

struct BatchFailureResponse final {
    std::uint16_t executed_operation_count{0U};
    std::uint16_t failed_operation{0U};
    bool retryable{false};
    base::Error error{base::ErrorCode::kInternal, "uninitialized RMDB batch failure"};

    auto operator==(const BatchFailureResponse&) const -> bool = default;
};

enum class TerminalResponseKind : std::uint8_t {
    kError,
    kTransactionAbort,
};

// A top-level protocol terminal. Input and PREPARE_SET failures use kError;
// only a retryable EXEC_STREAM failure that rolled back successfully uses
// kTransactionAbort. The response encoder maps this model to a terminal frame.
struct TerminalErrorResponse final {
    TerminalResponseKind kind{TerminalResponseKind::kError};
    base::Error error{base::ErrorCode::kInternal, "uninitialized RMDB terminal failure"};

    auto operator==(const TerminalErrorResponse&) const -> bool = default;
};

using RequestResponse = std::variant<
    StreamResponse,
    PrepareResponse,
    BatchSuccessResponse,
    BatchFailureResponse,
    TerminalErrorResponse>;

// Connection-local RMDB request semantics. It owns the prepared-statement
// dictionary, validates all client payloads before invoking a session port,
// atomically replaces a valid PREPARE_SET, and applies AUTO_ABORT policy to
// EXEC_BATCH. It deliberately does not own a socket or response encoder.
class RequestDispatcher final {
public:
    // session must outlive this dispatcher and every dispatch() call. The
    // dispatcher borrows the port but owns all connection-local prepared state.
    static auto create(SessionPort& session) -> base::Result<RequestDispatcher>;

    RequestDispatcher(const RequestDispatcher&) = delete;
    auto operator=(const RequestDispatcher&) -> RequestDispatcher& = delete;
    RequestDispatcher(RequestDispatcher&& other) noexcept;
    auto operator=(RequestDispatcher&& other) noexcept -> RequestDispatcher&;
    ~RequestDispatcher();

    // False only for the inert source left by a move. The source keeps no
    // prepared dictionary and dispatch() returns kUnavailable.
    auto valid() const noexcept -> bool;

    // Validates and executes one client-to-server RMDB frame. Expected peer
    // and SQL failures are returned as a RequestResponse terminal; allocation,
    // invariant, and unexpected-port failures remain structured Result errors.
    auto dispatch(const FrameView& frame) -> base::Result<RequestResponse>;

    auto prepared_statement_count() const noexcept -> std::size_t;

private:
    struct Impl;

    explicit RequestDispatcher(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::protocol
