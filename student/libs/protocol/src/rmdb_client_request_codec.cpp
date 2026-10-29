#include "rmdb_client_request_codec.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/base/text.hpp"
#include "db/protocol/session_port.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "network_byte_appender.hpp"

namespace db::protocol::internal {
namespace {

auto request_error(std::string message, const std::string_view operation) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "protocol.rmdb_client",
        std::string{operation}
    );
}

auto string_bytes(const std::string_view text) noexcept -> base::Bytes {
    return std::as_bytes(std::span{text.data(), text.size()});
}

auto ensure_request_room(
    const std::vector<std::byte>& output,
    const std::size_t additional,
    const std::string_view operation
) -> base::Status {
    if (additional > kMaximumRmdbFramePayloadSize - output.size()) {
        return base::fail(
            request_error("encoded RMDB request exceeded the 1 MiB protocol limit", operation)
        );
    }
    return {};
}

auto append_bound_value(
    std::vector<std::byte>& output,
    const types::Value& value,
    const WireDataType data_type,
    const std::string_view operation
) -> base::Status {
    if (value.is_null()) {
        auto room = ensure_request_room(output, 1U, operation);
        if (!room) {
            return room;
        }
        append_big_endian(output, std::uint8_t{0U});
        return {};
    }

    switch (data_type) {
        case WireDataType::kInt32: {
            const std::int64_t* const integer = value.as_integer();
            if (value.data_type().id() != types::kInt64TypeId || integer == nullptr
                || *integer < std::numeric_limits<std::int32_t>::min()
                || *integer > std::numeric_limits<std::int32_t>::max()) {
                return base::fail(request_error(
                    "RMDB INT32 parameter did not fit its declared wire type",
                    operation
                ));
            }
            auto room = ensure_request_room(output, 1U + sizeof(std::uint32_t), operation);
            if (!room) {
                return room;
            }
            append_big_endian(output, std::uint8_t{1U});
            append_big_endian(
                output,
                std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(*integer))
            );
            return {};
        }
        case WireDataType::kFloat32: {
            const float* const floating = value.as_float();
            if (value.data_type().id() != types::kFloat32TypeId || floating == nullptr) {
                return base::fail(request_error(
                    "RMDB FLOAT32 parameter did not match its declared wire type",
                    operation
                ));
            }
            auto room = ensure_request_room(output, 1U + sizeof(std::uint32_t), operation);
            if (!room) {
                return room;
            }
            append_big_endian(output, std::uint8_t{1U});
            append_big_endian(output, std::bit_cast<std::uint32_t>(*floating));
            return {};
        }
        case WireDataType::kChar: {
            const std::string* const character = value.as_string();
            if (value.data_type().id() != types::kCharTypeId || character == nullptr) {
                return base::fail(request_error(
                    "RMDB CHAR parameter did not match its declared wire type",
                    operation
                ));
            }
            if (!base::is_valid_utf8(*character)) {
                return base::fail(
                    request_error("RMDB CHAR parameter was not valid UTF-8", operation)
                );
            }
            if (character->size() > std::numeric_limits<std::uint32_t>::max()) {
                return base::fail(
                    request_error("RMDB CHAR parameter exceeded u32 length", operation)
                );
            }
            if (character->size() > kMaximumRmdbFramePayloadSize - 5U) {
                return base::fail(
                    request_error("RMDB CHAR parameter exceeded the 1 MiB request limit", operation)
                );
            }
            auto room = ensure_request_room(output, 5U + character->size(), operation);
            if (!room) {
                return room;
            }
            append_big_endian(output, std::uint8_t{1U});
            append_big_endian(output, static_cast<std::uint32_t>(character->size()));
            append_bytes(output, string_bytes(*character));
            return {};
        }
    }
    return base::fail(request_error("RMDB parameter used an unknown wire type", operation));
}

auto validate_prepared_statement_identity(
    const RmdbPreparedStatement& statement,
    const std::span<const RmdbPreparedStatement> statements,
    const std::size_t index
) -> base::Status {
    if (statement.statement_id == 0U) {
        return base::fail(
            request_error("PREPARE_SET statement IDs must be non-zero", "validate PREPARE_SET")
        );
    }
    for (std::size_t previous = 0U; previous < index; ++previous) {
        if (statements[previous].statement_id == statement.statement_id) {
            return base::fail(
                request_error("PREPARE_SET statement IDs must be unique", "validate PREPARE_SET")
            );
        }
    }
    return {};
}

auto validate_prepared_statement_shape(const RmdbPreparedStatement& statement) -> base::Status {
    auto sql = validate_rmdb_sql_request(statement.sql, "validate PREPARE_SET");
    if (!sql) {
        return sql;
    }
    for (const WireDataType data_type : statement.parameter_types) {
        if (!is_known_wire_data_type(data_type)) {
            return base::fail(request_error(
                "PREPARE_SET parameter types must use known RMDB types",
                "validate PREPARE_SET"
            ));
        }
    }
    switch (statement.result_kind) {
        case RmdbPreparedResultKind::kCommand:
            if (!statement.result_types.empty()) {
                return base::fail(request_error(
                    "prepared command statements must not declare result types",
                    "validate PREPARE_SET"
                ));
            }
            return {};
        case RmdbPreparedResultKind::kQuery:
            if (statement.result_types.empty()) {
                return base::fail(request_error(
                    "prepared query statements must declare at least one result type",
                    "validate PREPARE_SET"
                ));
            }
            for (const WireDataType data_type : statement.result_types) {
                if (!is_known_wire_data_type(data_type)) {
                    return base::fail(request_error(
                        "prepared query result types must use known RMDB types",
                        "validate PREPARE_SET"
                    ));
                }
            }
            return {};
    }
    return base::fail(
        request_error("PREPARE_SET used an unknown result kind", "validate PREPARE_SET")
    );
}

}  // namespace

auto validate_rmdb_sql_request(const std::string_view sql, const std::string_view operation)
    -> base::Status {
    if (sql.empty() || sql.contains('\0')) {
        return base::fail(
            request_error("RMDB SQL request is empty or contains a NUL byte", operation)
        );
    }
    if (sql.size() > kMaximumRmdbFramePayloadSize
        || sql.size() > std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(request_error("RMDB SQL request exceeded the 1 MiB limit", operation));
    }
    if (!base::is_valid_utf8(sql)) {
        return base::fail(request_error("RMDB SQL request was not valid UTF-8", operation));
    }
    return {};
}

auto validate_rmdb_prepared_statements(const std::span<const RmdbPreparedStatement> statements)
    -> base::Status {
    if (statements.empty() || statements.size() > kMaximumRmdbPreparedStatements) {
        return base::fail(
            request_error("PREPARE_SET statement count must be in 1..=256", "validate PREPARE_SET")
        );
    }
    for (std::size_t index = 0U; index < statements.size(); ++index) {
        const RmdbPreparedStatement& statement = statements[index];
        auto identity = validate_prepared_statement_identity(statement, statements, index);
        if (!identity) {
            return identity;
        }
        auto shape = validate_prepared_statement_shape(statement);
        if (!shape) {
            return shape;
        }
    }
    return {};
}

auto encode_rmdb_prepare_set(const std::span<const RmdbPreparedStatement> statements)
    -> base::Result<std::vector<std::byte>> {
    std::vector<std::byte> payload;
    auto initial_room = ensure_request_room(payload, sizeof(std::uint16_t), "encode PREPARE_SET");
    if (!initial_room) {
        return base::fail(std::move(initial_room).error());
    }
    append_big_endian(payload, static_cast<std::uint16_t>(statements.size()));
    for (const RmdbPreparedStatement& statement : statements) {
        const std::size_t fixed_size = sizeof(std::uint16_t) + sizeof(std::uint8_t)
                                       + sizeof(std::uint16_t) + statement.parameter_types.size()
                                       + sizeof(std::uint32_t);
        if (statement.sql.size() > kMaximumRmdbFramePayloadSize - fixed_size) {
            return base::fail(request_error(
                "PREPARE_SET statement exceeded the 1 MiB request limit",
                "encode PREPARE_SET"
            ));
        }
        auto room =
            ensure_request_room(payload, fixed_size + statement.sql.size(), "encode PREPARE_SET");
        if (!room) {
            return base::fail(std::move(room).error());
        }
        append_big_endian(payload, statement.statement_id);
        append_big_endian(
            payload,
            static_cast<std::uint8_t>(
                statement.result_kind == RmdbPreparedResultKind::kQuery ? 1U : 0U
            )
        );
        append_big_endian(payload, static_cast<std::uint16_t>(statement.parameter_types.size()));
        for (const WireDataType data_type : statement.parameter_types) {
            append_big_endian(payload, static_cast<std::uint8_t>(data_type));
        }
        append_big_endian(payload, static_cast<std::uint32_t>(statement.sql.size()));
        append_bytes(payload, string_bytes(statement.sql));
    }
    return payload;
}

auto encode_rmdb_batch_request(
    const RmdbPreparedCatalog& catalog,
    const std::span<const RmdbBatchOperation> operations
) -> base::Result<std::vector<std::byte>> {
    if (operations.empty() || operations.size() > kMaximumRmdbBatchOperations) {
        return base::fail(
            request_error("EXEC_BATCH operation count must be in 1..=256", "encode EXEC_BATCH")
        );
    }
    std::vector<std::byte> payload;
    auto initial_room = ensure_request_room(payload, sizeof(std::uint16_t), "encode EXEC_BATCH");
    if (!initial_room) {
        return base::fail(std::move(initial_room).error());
    }
    append_big_endian(payload, static_cast<std::uint16_t>(operations.size()));
    for (const RmdbBatchOperation& operation : operations) {
        const RmdbPreparedEntry* const entry = catalog.find(operation.statement_id);
        if (entry == nullptr) {
            return base::fail(request_error(
                "EXEC_BATCH referenced a statement absent from its prepared catalog",
                "encode EXEC_BATCH"
            ));
        }
        if (operation.parameters.size() != entry->parameter_types.size()) {
            return base::fail(request_error(
                "EXEC_BATCH parameter count did not match its prepared statement",
                "encode EXEC_BATCH"
            ));
        }
        auto statement_room =
            ensure_request_room(payload, sizeof(std::uint16_t), "encode EXEC_BATCH");
        if (!statement_room) {
            return base::fail(std::move(statement_room).error());
        }
        append_big_endian(payload, operation.statement_id);
        for (std::size_t parameter_index = 0U; parameter_index < operation.parameters.size();
             ++parameter_index) {
            auto encoded = append_bound_value(
                payload,
                operation.parameters[parameter_index],
                entry->parameter_types[parameter_index],
                "encode EXEC_BATCH"
            );
            if (!encoded) {
                return base::fail(std::move(encoded).error());
            }
        }
    }
    return payload;
}

}  // namespace db::protocol::internal
