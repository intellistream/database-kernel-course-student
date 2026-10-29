#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "db/base/result.hpp"
#include "db/types/value.hpp"

namespace db::protocol {

// The three SQL values representable by RMDB v3. The protocol owns this
// narrow vocabulary; an application adapter maps it to any richer engine type
// system without exposing that engine here.
enum class WireDataType : std::uint8_t {
    kInt32 = 0x01U,
    kFloat32 = 0x02U,
    kChar = 0x03U,
};

constexpr auto is_known_wire_data_type(const WireDataType data_type) noexcept -> bool {
    switch (data_type) {
        case WireDataType::kInt32:
        case WireDataType::kFloat32:
        case WireDataType::kChar:
            return true;
    }
    return false;
}

constexpr auto wire_data_type_from_tag(const std::uint8_t tag) noexcept
    -> std::optional<WireDataType> {
    switch (tag) {
        case static_cast<std::uint8_t>(WireDataType::kInt32):
            return WireDataType::kInt32;
        case static_cast<std::uint8_t>(WireDataType::kFloat32):
            return WireDataType::kFloat32;
        case static_cast<std::uint8_t>(WireDataType::kChar):
            return WireDataType::kChar;
    }
    return std::nullopt;
}

// RMDB INT32 is held in the project's non-narrowing INT64 Value, while the
// other wire scalars use their exact project value types.
constexpr auto wire_value_data_type(const WireDataType data_type) noexcept -> types::DataType {
    switch (data_type) {
        case WireDataType::kInt32:
            return types::kInt64Type;
        case WireDataType::kFloat32:
            return types::kFloat32Type;
        case WireDataType::kChar:
            return types::kCharType;
    }
    return types::kUnknownType;
}

struct ColumnMetadata final {
    std::string name;
    WireDataType data_type{WireDataType::kInt32};

    auto operator==(const ColumnMetadata&) const -> bool = default;
};

enum class ExecutionKind : std::uint8_t {
    kCommand,
    kQuery,
};

// One value already converted to the RMDB v3 result domain. Its alternative is
// selected together with ColumnMetadata by ExecutionResult::query(); response
// encoding therefore does not reinterpret a project Value or repeat text and
// integer representability checks.
class WireCell final {
public:
    WireCell(const WireCell& other);
    auto operator=(const WireCell& other) -> WireCell&;
    WireCell(WireCell&& other) noexcept = default;
    auto operator=(WireCell&& other) noexcept -> WireCell& = default;
    ~WireCell() = default;

    static auto null() noexcept -> WireCell;
    static auto integer(std::int32_t value) noexcept -> WireCell;
    static auto floating(float value) noexcept -> WireCell;
    static auto text(std::string value) noexcept -> WireCell;

    auto is_null() const noexcept -> bool;
    auto integer() const -> std::int32_t;
    auto floating() const -> float;
    auto text() const -> std::string_view;

    auto operator==(const WireCell&) const -> bool = default;

private:
    friend class ExecutionResult;

    using Storage = std::variant<std::monostate, std::int32_t, float, std::string>;

    explicit WireCell(Storage value) noexcept;

    Storage value_;
};

// A materialized protocol-facing execution result. A command is the default
// empty value. query() establishes column, row-width, type, range, and UTF-8
// invariants while converting project Values into final wire cells once.
class ExecutionResult final {
public:
    ExecutionResult() = default;

    static auto
    query(std::vector<ColumnMetadata> columns, std::vector<std::vector<types::Value>> rows)
        -> base::Result<ExecutionResult>;

    auto kind() const noexcept -> ExecutionKind;
    auto columns() const noexcept -> std::span<const ColumnMetadata>;
    auto rows() const noexcept -> std::span<const std::vector<WireCell>>;

    auto operator==(const ExecutionResult&) const -> bool = default;

private:
    ExecutionResult(
        std::vector<ColumnMetadata> columns,
        std::vector<std::vector<WireCell>> rows
    ) noexcept;

    ExecutionKind kind_{ExecutionKind::kCommand};
    std::vector<ColumnMetadata> columns_;
    std::vector<std::vector<WireCell>> rows_;
};

// One prepared statement belongs exclusively to one connection-local
// SessionPort. The adapter owns its engine plan; protocol owns this port and
// destroys it when replacing the connection's prepared dictionary.
class PreparedStatementPort {
public:
    virtual ~PreparedStatementPort() = default;

    PreparedStatementPort(const PreparedStatementPort&) = delete;
    auto operator=(const PreparedStatementPort&) -> PreparedStatementPort& = delete;
    PreparedStatementPort(PreparedStatementPort&&) = delete;
    auto operator=(PreparedStatementPort&&) -> PreparedStatementPort& = delete;

    virtual auto parameter_types() const noexcept -> std::span<const WireDataType> = 0;
    virtual auto result_columns() const noexcept -> std::span<const ColumnMetadata> = 0;
    virtual auto execute(std::span<const types::Value> parameters)
        -> base::Result<ExecutionResult> = 0;

protected:
    PreparedStatementPort() = default;
};

// A connection-local database/session boundary owned by protocol. An
// application composition root adapts this port to engine without exposing an
// engine Session, parser, planner, page, or executor type here. One returned
// port is confined to one connection worker. rollback() must be harmless for
// an idle session, and close() must roll back any active transaction before
// releasing its engine resources.
class SessionPort {
public:
    virtual ~SessionPort() = default;

    SessionPort(const SessionPort&) = delete;
    auto operator=(const SessionPort&) -> SessionPort& = delete;
    SessionPort(SessionPort&&) = delete;
    auto operator=(SessionPort&&) -> SessionPort& = delete;

    virtual auto execute_stream(std::string_view sql) -> base::Result<ExecutionResult> = 0;
    virtual auto prepare(std::string_view sql, std::span<const WireDataType> parameter_types)
        -> base::Result<std::unique_ptr<PreparedStatementPort>> = 0;
    virtual auto begin() -> base::Status = 0;
    virtual auto commit() -> base::Status = 0;
    virtual auto rollback() -> base::Status = 0;
    virtual auto close() -> base::Status = 0;

protected:
    SessionPort() = default;
};

// Creates one independent protocol session after a connection completes the
// RMDB preface exchange. The factory must outlive TcpServer and every active
// serve_* call, and must support concurrent open_session() calls when serve_n()
// permits multiple connections. The server owns each returned SessionPort
// exclusively.
class SessionFactoryPort {
public:
    virtual ~SessionFactoryPort() = default;

    SessionFactoryPort(const SessionFactoryPort&) = delete;
    auto operator=(const SessionFactoryPort&) -> SessionFactoryPort& = delete;
    SessionFactoryPort(SessionFactoryPort&&) = delete;
    auto operator=(SessionFactoryPort&&) -> SessionFactoryPort& = delete;

    virtual auto open_session() -> base::Result<std::unique_ptr<SessionPort>> = 0;

protected:
    SessionFactoryPort() = default;
};

}  // namespace db::protocol
