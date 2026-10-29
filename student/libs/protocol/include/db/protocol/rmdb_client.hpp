#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "db/base/result.hpp"
#include "db/io/socket.hpp"
#include "db/protocol/frame_codec.hpp"
#include "db/protocol/session_port.hpp"
#include "db/types/value.hpp"

namespace db::protocol {

// The declared shape of one PREPARE_SET member. Result types are a client-side
// contract: PREPARE_OK must return exactly this type sequence before a batch
// can use the statement. Column names remain server-defined metadata.
enum class RmdbPreparedResultKind : std::uint8_t {
    kCommand,
    kQuery,
};

struct RmdbPreparedStatement final {
    std::uint16_t statement_id{0U};
    std::string sql;
    std::vector<WireDataType> parameter_types;
    RmdbPreparedResultKind result_kind{RmdbPreparedResultKind::kCommand};
    std::vector<WireDataType> result_types;

    static auto command(
        std::uint16_t statement_id,
        std::string sql,
        std::vector<WireDataType> parameter_types = {}
    ) -> RmdbPreparedStatement;
    static auto query(
        std::uint16_t statement_id,
        std::string sql,
        std::vector<WireDataType> parameter_types,
        std::vector<WireDataType> result_types
    ) -> RmdbPreparedStatement;
};

// One validated entry in a connection-local prepared dictionary. It is an
// immutable value returned by prepare_set(); batch requests may only use the
// catalog that was returned from the same RmdbClient connection.
struct RmdbPreparedEntry final {
    std::uint16_t statement_id{0U};
    std::vector<WireDataType> parameter_types;
    RmdbPreparedResultKind result_kind{RmdbPreparedResultKind::kCommand};
    std::vector<ColumnMetadata> columns;
};

class RmdbPreparedCatalog final {
public:
    RmdbPreparedCatalog() = default;

    auto contains(std::uint16_t statement_id) const noexcept -> bool;
    auto find(std::uint16_t statement_id) const noexcept -> const RmdbPreparedEntry*;
    auto entries() const noexcept -> std::span<const RmdbPreparedEntry>;

private:
    std::vector<RmdbPreparedEntry> entries_;
    std::shared_ptr<const std::byte> connection_token_;

    friend class RmdbClient;
};

struct RmdbBatchOperation final {
    std::uint16_t statement_id{0U};
    std::vector<types::Value> parameters;
};

enum class RmdbTerminalKind : std::uint8_t {
    kCommandOk,
    kResultEnd,
    kTransactionAbort,
    kError,
};

// A complete EXEC_STREAM terminal. row_count is meaningful only for
// kResultEnd; diagnostic is meaningful only for non-success terminals.
struct RmdbTerminal final {
    RmdbTerminalKind kind{RmdbTerminalKind::kError};
    std::uint64_t row_count{0U};
    std::string diagnostic;

    auto is_success() const noexcept -> bool;
    auto is_transaction_abort() const noexcept -> bool;
};

struct RmdbResponse final {
    std::vector<ColumnMetadata> columns;
    std::vector<std::vector<types::Value>> rows;
    RmdbTerminal terminal;
};

enum class RmdbBatchTerminalKind : std::uint8_t {
    kOk,
    kTransactionAbort,
    kError,
};

struct RmdbBatchTerminal final {
    RmdbBatchTerminalKind kind{RmdbBatchTerminalKind::kError};
    std::uint16_t failed_operation{0U};
    std::string diagnostic;

    auto is_success() const noexcept -> bool;
    auto is_transaction_abort() const noexcept -> bool;
};

struct RmdbBatchResponse final {
    std::uint16_t executed_operation_count{0U};
    std::vector<std::optional<RmdbResponse>> responses;
    RmdbBatchTerminal terminal;
};

struct RmdbClientOptions final {
    // Every preface, request write, and complete response has one absolute
    // deadline of this duration. A zero duration is rejected at connect().
    std::chrono::milliseconds io_timeout{5000};
    // The sum of response-frame payload bytes for one operation. This bounds
    // materialization independently of the per-frame protocol maximum.
    std::size_t maximum_response_bytes{kMaximumRmdbFramePayloadSize};
};

// Move-only RMDB v3 client. It uses the shared bounded frame codec, but owns
// its TCP connection and all response decoding state. Calls are serialized by
// the client object; one RmdbClient belongs to one application worker.
class RmdbClient final {
public:
    static auto connect(const io::TcpEndpoint& endpoint, RmdbClientOptions options = {})
        -> base::Result<RmdbClient>;

    RmdbClient(const RmdbClient&) = delete;
    auto operator=(const RmdbClient&) -> RmdbClient& = delete;
    RmdbClient(RmdbClient&& other) noexcept;
    auto operator=(RmdbClient&& other) noexcept -> RmdbClient&;
    ~RmdbClient();

    auto is_open() const noexcept -> bool;
    auto close() -> base::Status;

    // Sends one EXEC_STREAM request and materializes its bounded response.
    // SQL or peer protocol errors are returned as a successful Result holding
    // a kError/kTransactionAbort terminal; malformed peer bytes are Result
    // errors with kCorruption.
    auto execute(std::string_view sql) -> base::Result<RmdbResponse>;

    // Atomically replaces the server's prepared dictionary and returns the
    // checked catalog needed by execute_batch().
    auto prepare_set(std::span<const RmdbPreparedStatement> statements)
        -> base::Result<RmdbPreparedCatalog>;

    // Sends EXEC_BATCH with mandatory AUTO_ABORT and returns the packed batch
    // terminal. The supplied catalog must have come from this same connection.
    auto execute_batch(
        const RmdbPreparedCatalog& catalog,
        std::span<const RmdbBatchOperation> operations
    ) -> base::Result<RmdbBatchResponse>;

private:
    struct Impl;

    explicit RmdbClient(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::protocol
