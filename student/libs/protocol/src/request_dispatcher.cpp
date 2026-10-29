#include "db/protocol/request_dispatcher.hpp"

#include <algorithm>
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
#include <variant>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/base/text.hpp"
#include "db/types/data_type.hpp"
#include "db/types/type_io.hpp"

namespace db::protocol {
namespace {

inline constexpr base::ExceptionBoundaryPolicy kDispatcherExceptionBoundary{
    .component = "protocol.request_dispatcher",
    .allocation_message = "RMDB request processing could not allocate bounded state",
    .exception_prefix = "unexpected exception escaped a protocol port: ",
    .unknown_exception_message = "unknown exception escaped a protocol port",
};

auto dispatch_error(
    const base::ErrorCode code,
    std::string message,
    const std::string_view operation
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "protocol.request_dispatcher",
        std::string{operation}
    );
}

auto payload_error(std::string message, const std::string_view operation) -> base::Error {
    return dispatch_error(base::ErrorCode::kInvalidArgument, std::move(message), operation);
}

auto invariant_error(std::string message, const std::string_view operation) -> base::Error {
    return dispatch_error(base::ErrorCode::kInvariantViolation, std::move(message), operation);
}

auto is_fatal_port_error(const base::Error& error) noexcept -> bool {
    switch (error.code()) {
        case base::ErrorCode::kInvariantViolation:
        case base::ErrorCode::kResourceExhausted:
        case base::ErrorCode::kInternal:
            return true;
        default:
            return false;
    }
}

auto rollback_failure(
    const base::Error& original,
    const base::Error& rollback_error,
    const std::string_view operation
) -> base::Error {
    std::string message = original.to_string();
    message.append("; automatic rollback failed: ");
    message.append(rollback_error.to_string());
    return dispatch_error(base::ErrorCode::kExecution, std::move(message), operation);
}

auto bytes_to_string(const base::Bytes bytes) -> std::string {
    std::string result;
    result.reserve(bytes.size());
    for (const std::byte byte : bytes) {
        result.push_back(static_cast<char>(std::to_integer<std::uint8_t>(byte)));
    }
    return result;
}

auto validate_utf8(
    const std::string_view text,
    const std::string_view field,
    const std::string_view operation
) -> base::Status {
    if (!base::is_valid_utf8(text)) {
        return base::fail(payload_error(
            std::string{"RMDB "}.append(field).append(" was not valid UTF-8"),
            operation
        ));
    }
    return {};
}

template <typename Value>
auto required_field(
    base::Result<Value> field,
    const std::string_view name,
    const std::string_view operation
) -> base::Result<Value> {
    if (!field) {
        return base::fail(payload_error(
            std::string{"RMDB request payload was truncated while reading "}.append(name),
            operation
        ));
    }
    return std::move(field).value();
}

auto decode_wire_data_type(const std::uint8_t tag, const std::string_view operation)
    -> base::Result<WireDataType> {
    const auto decoded = wire_data_type_from_tag(tag);
    if (!decoded.has_value()) {
        return base::fail(payload_error("RMDB request used an unknown SQL type tag", operation));
    }
    return decoded.value();
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
        return base::fail(payload_error("RMDB cell used an invalid null marker", operation));
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
            static_assert(sizeof(float) == sizeof(std::uint32_t));
            auto bits = required_field(reader.read_u32(), "FLOAT32 cell", operation);
            if (!bits) {
                return base::fail(std::move(bits).error());
            }
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
            auto text = bytes_to_string(bytes.value());
            if (!base::is_valid_utf8(text)) {
                return base::fail(payload_error("RMDB CHAR cell was not valid UTF-8", operation));
            }
            return types::Value::character(std::move(text));
        }
    }
    return base::fail(invariant_error("unknown RMDB cell type reached decoder", operation));
}

auto decode_sql(const base::Bytes payload, const std::string_view operation)
    -> base::Result<std::string> {
    if (payload.empty()) {
        return base::fail(payload_error("RMDB SQL request is empty", operation));
    }
    if (std::find(payload.begin(), payload.end(), std::byte{0}) != payload.end()) {
        return base::fail(payload_error("RMDB SQL request contains a NUL byte", operation));
    }
    std::string sql = bytes_to_string(payload);
    auto utf8 = validate_utf8(sql, "SQL request", operation);
    if (!utf8) {
        return base::fail(std::move(utf8).error());
    }
    return sql;
}

enum class PreparedControl : std::uint8_t {
    kBegin,
    kCommit,
    kRollback,
};

auto parse_prepared_control(std::string_view sql) noexcept -> std::optional<PreparedControl> {
    sql = base::trim_ascii_space(sql);
    if (!sql.empty() && sql.back() == ';') {
        sql.remove_suffix(1U);
        sql = base::trim_ascii_space(sql);
    }
    if (base::ascii_equal_ignoring_case(sql, "begin")) {
        return PreparedControl::kBegin;
    }
    if (base::ascii_equal_ignoring_case(sql, "commit")) {
        return PreparedControl::kCommit;
    }
    if (base::ascii_equal_ignoring_case(sql, "abort")
        || base::ascii_equal_ignoring_case(sql, "rollback")) {
        return PreparedControl::kRollback;
    }
    return std::nullopt;
}

constexpr auto ascii_digit(const unsigned char character) noexcept -> bool {
    return character >= static_cast<unsigned char>('0')
           && character <= static_cast<unsigned char>('9');
}

enum class SqlScanState : std::uint8_t {
    kNormal,
    kSingleQuote,
    kDoubleQuote,
    kLineComment,
    kBlockComment,
};

void consume_non_normal_sql_character(
    const std::string_view sql,
    std::size_t& index,
    SqlScanState& state
) noexcept {
    const char current = sql[index];
    const char next = index + 1U < sql.size() ? sql[index + 1U] : '\0';
    switch (state) {
        case SqlScanState::kSingleQuote:
            if (current == '\'' && next == '\'') {
                index += 2U;
                return;
            }
            if (current == '\'') {
                state = SqlScanState::kNormal;
            }
            ++index;
            return;
        case SqlScanState::kDoubleQuote:
            if (current == '"' && next == '"') {
                index += 2U;
                return;
            }
            if (current == '"') {
                state = SqlScanState::kNormal;
            }
            ++index;
            return;
        case SqlScanState::kLineComment:
            if (current == '\n' || current == '\r') {
                state = SqlScanState::kNormal;
            }
            ++index;
            return;
        case SqlScanState::kBlockComment:
            if (current == '*' && next == '/') {
                state = SqlScanState::kNormal;
                index += 2U;
                return;
            }
            ++index;
            return;
        case SqlScanState::kNormal:
            return;
    }
}

auto begin_sql_quote_or_comment(
    const std::string_view sql,
    std::size_t& index,
    SqlScanState& state
) noexcept -> bool {
    const char current = sql[index];
    const char next = index + 1U < sql.size() ? sql[index + 1U] : '\0';
    if (current == '\'') {
        state = SqlScanState::kSingleQuote;
        ++index;
        return true;
    }
    if (current == '"') {
        state = SqlScanState::kDoubleQuote;
        ++index;
        return true;
    }
    if (current == '-' && next == '-') {
        state = SqlScanState::kLineComment;
        index += 2U;
        return true;
    }
    if (current == '/' && next == '*') {
        state = SqlScanState::kBlockComment;
        index += 2U;
        return true;
    }
    return false;
}

auto parse_parameter_ordinal(
    const std::string_view sql,
    std::size_t& index,
    const std::string_view operation
) -> base::Result<std::size_t> {
    std::size_t cursor = index + 1U;
    if (cursor == sql.size() || !ascii_digit(static_cast<unsigned char>(sql[cursor]))) {
        return base::fail(
            payload_error("RMDB SQL used a '$' without a parameter ordinal", operation)
        );
    }

    std::size_t ordinal = 0U;
    while (cursor < sql.size() && ascii_digit(static_cast<unsigned char>(sql[cursor]))) {
        const std::size_t digit = static_cast<std::size_t>(sql[cursor] - '0');
        if (ordinal > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
            return base::fail(payload_error("RMDB SQL parameter ordinal overflowed", operation));
        }
        ordinal = ordinal * 10U + digit;
        ++cursor;
    }
    index = cursor;
    return ordinal;
}

auto scan_ended_inside_sql_region(const SqlScanState state) noexcept -> bool {
    return state == SqlScanState::kSingleQuote || state == SqlScanState::kDoubleQuote
           || state == SqlScanState::kBlockComment;
}

auto validate_parameter_markers(
    const std::string_view sql,
    const std::size_t parameter_count,
    const std::string_view operation
) -> base::Status {
    std::vector<std::uint8_t> seen(parameter_count, 0U);
    SqlScanState state = SqlScanState::kNormal;
    std::size_t index = 0U;
    while (index < sql.size()) {
        if (state != SqlScanState::kNormal) {
            consume_non_normal_sql_character(sql, index, state);
            continue;
        }
        if (begin_sql_quote_or_comment(sql, index, state)) {
            continue;
        }
        if (sql[index] != '$') {
            ++index;
            continue;
        }

        auto ordinal = parse_parameter_ordinal(sql, index, operation);
        if (!ordinal) {
            return base::fail(std::move(ordinal).error());
        }
        if (ordinal.value() == 0U || ordinal.value() > parameter_count) {
            return base::fail(payload_error(
                "RMDB SQL parameter ordinal is outside the declared parameter range",
                operation
            ));
        }
        seen[ordinal.value() - 1U] = 1U;
    }

    if (scan_ended_inside_sql_region(state)) {
        return base::fail(
            payload_error("RMDB SQL ended inside a quoted or comment region", operation)
        );
    }
    if (std::find(seen.begin(), seen.end(), 0U) != seen.end()) {
        return base::fail(
            payload_error("not every declared RMDB parameter appears in SQL", operation)
        );
    }
    return {};
}

enum class DeclaredResultKind : std::uint8_t {
    kCommand,
    kQuery,
};

auto decode_result_kind(const std::uint8_t tag, const std::string_view operation)
    -> base::Result<DeclaredResultKind> {
    switch (tag) {
        case 0U:
            return DeclaredResultKind::kCommand;
        case 1U:
            return DeclaredResultKind::kQuery;
    }
    return base::fail(payload_error("PREPARE_SET used an unknown result kind", operation));
}

struct PreparedDefinition final {
    std::uint16_t statement_id{0U};
    DeclaredResultKind result_kind{DeclaredResultKind::kCommand};
    std::vector<WireDataType> parameter_types;
    std::string sql;
};

auto contains_statement_id(
    const std::vector<PreparedDefinition>& definitions,
    const std::uint16_t statement_id
) noexcept -> bool {
    return std::any_of(
        definitions.begin(),
        definitions.end(),
        [statement_id](const PreparedDefinition& definition) {
            return definition.statement_id == statement_id;
        }
    );
}

auto decode_prepared_parameter_types(
    base::BigEndianReader& reader,
    const std::string_view operation
) -> base::Result<std::vector<WireDataType>> {
    auto parameter_count =
        required_field(reader.read_u16(), "PREPARE_SET parameter count", operation);
    if (!parameter_count) {
        return base::fail(std::move(parameter_count).error());
    }
    if (parameter_count.value() > reader.remaining()) {
        return base::fail(payload_error("PREPARE_SET parameter type list is truncated", operation));
    }

    std::vector<WireDataType> parameter_types;
    parameter_types.reserve(parameter_count.value());
    for (std::size_t parameter_index = 0U; parameter_index < parameter_count.value();
         ++parameter_index) {
        auto type_tag = required_field(reader.read_u8(), "PREPARE_SET parameter type", operation);
        if (!type_tag) {
            return base::fail(std::move(type_tag).error());
        }
        auto data_type = decode_wire_data_type(type_tag.value(), operation);
        if (!data_type) {
            return base::fail(std::move(data_type).error());
        }
        parameter_types.push_back(data_type.value());
    }
    return parameter_types;
}

auto decode_prepared_definition(
    base::BigEndianReader& reader,
    const std::vector<PreparedDefinition>& definitions,
    const std::string_view operation
) -> base::Result<PreparedDefinition> {
    auto statement_id = required_field(reader.read_u16(), "PREPARE_SET statement id", operation);
    if (!statement_id) {
        return base::fail(std::move(statement_id).error());
    }
    if (statement_id.value() == 0U || contains_statement_id(definitions, statement_id.value())) {
        return base::fail(
            payload_error("PREPARE_SET used a zero or duplicate statement id", operation)
        );
    }
    auto result_tag = required_field(reader.read_u8(), "PREPARE_SET result kind", operation);
    if (!result_tag) {
        return base::fail(std::move(result_tag).error());
    }
    auto result_kind = decode_result_kind(result_tag.value(), operation);
    if (!result_kind) {
        return base::fail(std::move(result_kind).error());
    }
    auto parameter_types = decode_prepared_parameter_types(reader, operation);
    if (!parameter_types) {
        return base::fail(std::move(parameter_types).error());
    }
    auto sql_length = required_field(reader.read_u32(), "PREPARE_SET SQL length", operation);
    if (!sql_length) {
        return base::fail(std::move(sql_length).error());
    }
    auto sql_bytes = required_field(
        reader.read_bytes(static_cast<std::size_t>(sql_length.value())),
        "PREPARE_SET SQL",
        operation
    );
    if (!sql_bytes) {
        return base::fail(std::move(sql_bytes).error());
    }
    auto sql = decode_sql(sql_bytes.value(), operation);
    if (!sql) {
        return base::fail(std::move(sql).error());
    }
    auto markers =
        validate_parameter_markers(sql.value(), parameter_types.value().size(), operation);
    if (!markers) {
        return base::fail(std::move(markers).error());
    }
    return PreparedDefinition{
        .statement_id = statement_id.value(),
        .result_kind = result_kind.value(),
        .parameter_types = std::move(parameter_types).value(),
        .sql = std::move(sql).value(),
    };
}

auto decode_prepare_set(const base::Bytes payload)
    -> base::Result<std::vector<PreparedDefinition>> {
    constexpr std::string_view operation = "decode PREPARE_SET";
    base::BigEndianReader reader{payload};
    auto count = required_field(reader.read_u16(), "PREPARE_SET statement count", operation);
    if (!count) {
        return base::fail(std::move(count).error());
    }
    if (count.value() == 0U || count.value() > kMaximumRmdbPreparedStatements) {
        return base::fail(
            payload_error("PREPARE_SET statement count is outside 1..=256", operation)
        );
    }

    std::vector<PreparedDefinition> definitions;
    definitions.reserve(count.value());
    for (std::size_t entry_index = 0U; entry_index < count.value(); ++entry_index) {
        auto definition = decode_prepared_definition(reader, definitions, operation);
        if (!definition) {
            return base::fail(std::move(definition).error());
        }
        definitions.push_back(std::move(definition).value());
    }
    auto finished = reader.finish();
    if (!finished) {
        return base::fail(payload_error("PREPARE_SET payload has trailing bytes", operation));
    }
    return definitions;
}

struct PreparedEntry final {
    std::vector<WireDataType> parameter_types;
    std::vector<ColumnMetadata> columns;
    std::variant<PreparedControl, std::unique_ptr<PreparedStatementPort>> executable;
};

class PreparedCatalog final {
public:
    auto find(const std::uint16_t statement_id) noexcept -> PreparedEntry* {
        const std::size_t slot = statement_id;
        if (slot >= entry_indices_.size()) {
            return nullptr;
        }
        const std::uint16_t index = entry_indices_[slot];
        if (index == kMissingEntry || index >= entries_.size()) {
            return nullptr;
        }
        return &entries_[index];
    }

    auto find(const std::uint16_t statement_id) const noexcept -> const PreparedEntry* {
        const std::size_t slot = statement_id;
        if (slot >= entry_indices_.size()) {
            return nullptr;
        }
        const std::uint16_t index = entry_indices_[slot];
        if (index == kMissingEntry || index >= entries_.size()) {
            return nullptr;
        }
        return &entries_[index];
    }

    void insert(const std::uint16_t statement_id, PreparedEntry entry) {
        const std::size_t slot = statement_id;
        if (entry_indices_.size() <= slot) {
            entry_indices_.resize(slot + 1U, kMissingEntry);
        }
        const auto entry_index = static_cast<std::uint16_t>(entries_.size());
        entry_indices_[slot] = entry_index;
        entries_.push_back(std::move(entry));
        order_.push_back(statement_id);
    }

    auto metadata() const -> base::Result<std::vector<PreparedStatementMetadata>> {
        std::vector<PreparedStatementMetadata> result;
        result.reserve(order_.size());
        for (const std::uint16_t statement_id : order_) {
            const PreparedEntry* const entry = find(statement_id);
            if (entry == nullptr) {
                return base::fail(invariant_error(
                    "prepared catalog order named a missing statement",
                    "build PREPARE_OK metadata"
                ));
            }
            result.push_back(
                PreparedStatementMetadata{
                    .statement_id = statement_id,
                    .columns = entry->columns,
                }
            );
        }
        return result;
    }

    auto size() const noexcept -> std::size_t {
        return entries_.size();
    }

private:
    static constexpr std::uint16_t kMissingEntry = std::numeric_limits<std::uint16_t>::max();

    std::vector<PreparedEntry> entries_;
    std::vector<std::uint16_t> entry_indices_;
    std::vector<std::uint16_t> order_;
};

auto validate_columns(
    const std::span<const ColumnMetadata> columns,
    const bool require_nonempty,
    const std::string_view operation
) -> base::Status {
    if (columns.size() > std::numeric_limits<std::uint16_t>::max()) {
        return base::fail(invariant_error(
            "protocol session port returned too many result columns for RMDB",
            operation
        ));
    }
    if (require_nonempty && columns.empty()) {
        return base::fail(invariant_error(
            "protocol session port returned a query without result columns",
            operation
        ));
    }
    for (const ColumnMetadata& column : columns) {
        if (column.name.empty()) {
            return base::fail(invariant_error(
                "protocol session port returned an empty result column name",
                operation
            ));
        }
        if (column.name.size() > std::numeric_limits<std::uint16_t>::max()) {
            return base::fail(invariant_error(
                "protocol session port returned an oversized result column name",
                operation
            ));
        }
        if (!is_known_wire_data_type(column.data_type)) {
            return base::fail(invariant_error(
                "protocol session port returned an unknown result column type",
                operation
            ));
        }
        auto utf8 = validate_utf8(column.name, "result column name", operation);
        if (!utf8) {
            return base::fail(invariant_error(
                "protocol session port returned a non-UTF-8 result column name",
                operation
            ));
        }
    }
    return {};
}

auto bind_prepared_control(const PreparedDefinition& definition, const PreparedControl control)
    -> base::Result<PreparedEntry> {
    if (!definition.parameter_types.empty()
        || definition.result_kind != DeclaredResultKind::kCommand) {
        return base::fail(payload_error(
            "prepared transaction control statement has an invalid shape",
            "bind PREPARE_SET"
        ));
    }
    return PreparedEntry{
        .parameter_types = {},
        .columns = {},
        .executable = control,
    };
}

auto bind_prepared_statement(SessionPort& session, const PreparedDefinition& definition)
    -> base::Result<PreparedEntry> {
    auto prepared = session.prepare(definition.sql, definition.parameter_types);
    if (!prepared) {
        return base::fail(
            std::move(prepared).error().with_context(
                "protocol.request_dispatcher",
                "prepare statement"
            )
        );
    }
    if (!prepared.value()) {
        return base::fail(invariant_error(
            "protocol session port returned a null prepared statement",
            "bind PREPARE_SET"
        ));
    }

    const PreparedStatementPort& statement = *prepared.value();
    if (!std::equal(
            statement.parameter_types().begin(),
            statement.parameter_types().end(),
            definition.parameter_types.begin(),
            definition.parameter_types.end()
        )) {
        return base::fail(invariant_error(
            "protocol session port returned a different prepared parameter layout",
            "bind PREPARE_SET"
        ));
    }

    PreparedEntry entry{
        .parameter_types = definition.parameter_types,
        .columns = {},
        .executable = PreparedControl::kBegin,
    };
    entry.columns.assign(statement.result_columns().begin(), statement.result_columns().end());
    const bool is_query = !entry.columns.empty();
    auto columns = validate_columns(entry.columns, is_query, "bind PREPARE_SET");
    if (!columns) {
        return base::fail(std::move(columns).error());
    }
    const auto actual_kind = is_query ? DeclaredResultKind::kQuery : DeclaredResultKind::kCommand;
    if (actual_kind != definition.result_kind) {
        return base::fail(payload_error(
            "prepared statement result kind does not match its bound shape",
            "bind PREPARE_SET"
        ));
    }
    entry.executable = std::move(prepared).value();
    return entry;
}

auto bind_prepared_definition(SessionPort& session, const PreparedDefinition& definition)
    -> base::Result<PreparedEntry> {
    const auto control = parse_prepared_control(definition.sql);
    if (control.has_value()) {
        return bind_prepared_control(definition, *control);
    }
    return bind_prepared_statement(session, definition);
}

auto build_prepared_catalog(
    SessionPort& session,
    const std::vector<PreparedDefinition>& definitions
) -> base::Result<PreparedCatalog> {
    PreparedCatalog catalog;
    for (const PreparedDefinition& definition : definitions) {
        auto entry = bind_prepared_definition(session, definition);
        if (!entry) {
            return base::fail(std::move(entry).error());
        }
        catalog.insert(definition.statement_id, std::move(entry).value());
    }
    return catalog;
}

struct DecodedBatchOperation final {
    std::uint16_t statement_id{0U};
    std::vector<types::Value> parameters;
};

auto decode_batch_operations(const base::Bytes payload, const PreparedCatalog& catalog)
    -> base::Result<std::vector<DecodedBatchOperation>> {
    constexpr std::string_view operation = "decode EXEC_BATCH";
    base::BigEndianReader reader{payload};
    auto count = required_field(reader.read_u16(), "EXEC_BATCH operation count", operation);
    if (!count) {
        return base::fail(std::move(count).error());
    }
    if (count.value() == 0U || count.value() > kMaximumRmdbBatchOperations) {
        return base::fail(
            payload_error("EXEC_BATCH operation count is outside 1..=256", operation)
        );
    }
    std::vector<DecodedBatchOperation> operations;
    operations.reserve(count.value());
    for (std::size_t operation_index = 0U; operation_index < count.value(); ++operation_index) {
        auto statement_id = required_field(reader.read_u16(), "EXEC_BATCH statement id", operation);
        if (!statement_id) {
            return base::fail(std::move(statement_id).error());
        }
        const PreparedEntry* const entry = catalog.find(statement_id.value());
        if (entry == nullptr) {
            return base::fail(
                payload_error("EXEC_BATCH referenced an unknown prepared statement", operation)
            );
        }
        std::vector<types::Value> parameters;
        parameters.reserve(entry->parameter_types.size());
        for (const WireDataType parameter_type : entry->parameter_types) {
            auto parameter = decode_cell(reader, parameter_type, operation);
            if (!parameter) {
                return base::fail(std::move(parameter).error());
            }
            parameters.push_back(std::move(parameter).value());
        }
        operations.push_back(
            DecodedBatchOperation{
                .statement_id = statement_id.value(),
                .parameters = std::move(parameters),
            }
        );
    }
    auto finished = reader.finish();
    if (!finished) {
        return base::fail(payload_error("EXEC_BATCH payload has trailing bytes", operation));
    }
    return operations;
}

auto execute_prepared_entry(
    SessionPort& session,
    PreparedEntry& entry,
    const std::span<const types::Value> parameters
) -> base::Result<ExecutionResult> {
    if (const auto* const control = std::get_if<PreparedControl>(&entry.executable);
        control != nullptr) {
        base::Status status;
        switch (*control) {
            case PreparedControl::kBegin:
                status = session.begin();
                break;
            case PreparedControl::kCommit:
                status = session.commit();
                break;
            case PreparedControl::kRollback:
                status = session.rollback();
                break;
        }
        if (!status) {
            return base::fail(std::move(status).error());
        }
        return ExecutionResult{};
    }

    auto& prepared = std::get<std::unique_ptr<PreparedStatementPort>>(entry.executable);
    if (!prepared) {
        return base::fail(
            invariant_error("prepared catalog retained a null statement port", "execute EXEC_BATCH")
        );
    }
    auto execution = prepared->execute(parameters);
    if (!execution) {
        return base::fail(
            std::move(execution).error().with_context(
                "protocol.request_dispatcher",
                "execute prepared statement"
            )
        );
    }
    if (entry.columns.empty()) {
        if (execution->kind() != ExecutionKind::kCommand) {
            return base::fail(
                invariant_error("prepared command returned a query result", "execute EXEC_BATCH")
            );
        }
    } else if (
        execution->kind() != ExecutionKind::kQuery
        || !std::equal(
            execution->columns().begin(),
            execution->columns().end(),
            entry.columns.begin(),
            entry.columns.end()
        )
    ) {
        return base::fail(invariant_error(
            "prepared query result does not match PREPARE_SET metadata",
            "execute EXEC_BATCH"
        ));
    }
    return std::move(execution).value();
}

auto terminal_error(const TerminalResponseKind kind, base::Error error) -> TerminalErrorResponse {
    return TerminalErrorResponse{.kind = kind, .error = std::move(error)};
}

auto rollback_to_terminal(SessionPort& session, base::Error error, const std::string_view operation)
    -> base::Result<RequestResponse> {
    auto rollback = session.rollback();
    if (!rollback) {
        return RequestResponse{terminal_error(
            TerminalResponseKind::kError,
            rollback_failure(error, rollback.error(), operation)
        )};
    }
    return RequestResponse{terminal_error(TerminalResponseKind::kError, std::move(error))};
}

auto handle_stream(SessionPort& session, const base::Bytes payload)
    -> base::Result<RequestResponse> {
    auto sql = decode_sql(payload, "decode EXEC_STREAM");
    if (!sql) {
        return RequestResponse{
            terminal_error(TerminalResponseKind::kError, std::move(sql).error())
        };
    }
    auto execution = session.execute_stream(sql.value());
    if (!execution) {
        base::Error error = std::move(execution).error();
        if (is_fatal_port_error(error)) {
            return base::fail(std::move(error));
        }
        if (!error.is_retryable()) {
            return RequestResponse{terminal_error(TerminalResponseKind::kError, std::move(error))};
        }
        auto rollback = session.rollback();
        if (!rollback) {
            return RequestResponse{terminal_error(
                TerminalResponseKind::kError,
                rollback_failure(error, rollback.error(), "execute EXEC_STREAM")
            )};
        }
        return RequestResponse{
            terminal_error(TerminalResponseKind::kTransactionAbort, std::move(error))
        };
    }
    return RequestResponse{StreamResponse{.execution = std::move(execution).value()}};
}

auto handle_prepare_set(SessionPort& session, PreparedCatalog& current, const base::Bytes payload)
    -> base::Result<RequestResponse> {
    auto definitions = decode_prepare_set(payload);
    if (!definitions) {
        return RequestResponse{
            terminal_error(TerminalResponseKind::kError, std::move(definitions).error())
        };
    }
    auto candidate = build_prepared_catalog(session, definitions.value());
    if (!candidate) {
        base::Error error = std::move(candidate).error();
        if (is_fatal_port_error(error)) {
            return base::fail(std::move(error));
        }
        return RequestResponse{terminal_error(TerminalResponseKind::kError, std::move(error))};
    }
    auto metadata = candidate.value().metadata();
    if (!metadata) {
        return base::fail(std::move(metadata).error());
    }
    current = std::move(candidate).value();
    return RequestResponse{PrepareResponse{.statements = std::move(metadata).value()}};
}

auto handle_batch(SessionPort& session, PreparedCatalog& catalog, const base::Bytes payload)
    -> base::Result<RequestResponse> {
    auto operations = decode_batch_operations(payload, catalog);
    if (!operations) {
        return rollback_to_terminal(session, std::move(operations).error(), "decode EXEC_BATCH");
    }

    std::vector<BatchQueryResult> query_results;
    for (std::size_t operation_index = 0U; operation_index < operations.value().size();
         ++operation_index) {
        DecodedBatchOperation& operation = operations.value()[operation_index];
        PreparedEntry* const entry = catalog.find(operation.statement_id);
        if (entry == nullptr) {
            base::Error error = invariant_error(
                "decoded batch operation disappeared from its prepared catalog",
                "execute EXEC_BATCH"
            );
            auto rollback = session.rollback();
            if (!rollback) {
                return base::fail(rollback_failure(error, rollback.error(), "execute EXEC_BATCH"));
            }
            return base::fail(std::move(error));
        }
        auto execution = execute_prepared_entry(session, *entry, operation.parameters);
        if (!execution) {
            base::Error error = std::move(execution).error();
            if (is_fatal_port_error(error)) {
                auto rollback = session.rollback();
                if (!rollback) {
                    return base::fail(
                        rollback_failure(error, rollback.error(), "execute EXEC_BATCH")
                    );
                }
                return base::fail(std::move(error));
            }
            const bool retryable = error.is_retryable();
            auto rollback = session.rollback();
            if (!rollback) {
                return RequestResponse{terminal_error(
                    TerminalResponseKind::kError,
                    rollback_failure(error, rollback.error(), "execute EXEC_BATCH")
                )};
            }
            const auto index = static_cast<std::uint16_t>(operation_index);
            return RequestResponse{BatchFailureResponse{
                .executed_operation_count = index,
                .failed_operation = index,
                .retryable = retryable,
                .error = std::move(error),
            }};
        }
        if (execution->kind() == ExecutionKind::kQuery) {
            query_results.push_back(
                BatchQueryResult{
                    .operation_index = static_cast<std::uint16_t>(operation_index),
                    .execution = std::move(execution).value(),
                }
            );
        }
    }
    return RequestResponse{BatchSuccessResponse{
        .executed_operation_count = static_cast<std::uint16_t>(operations.value().size()),
        .query_results = std::move(query_results),
    }};
}

auto dispatch_request(SessionPort& session, PreparedCatalog& catalog, const FrameView& frame)
    -> base::Result<RequestResponse> {
    auto header = validate_inbound_frame_envelope(FrameDirection::kClientToServer, frame);
    if (!header) {
        return RequestResponse{
            terminal_error(TerminalResponseKind::kError, std::move(header).error())
        };
    }
    switch (header.value().tag) {
        case FrameTag::kExecStream:
            return handle_stream(session, frame.payload);
        case FrameTag::kPrepareSet:
            return handle_prepare_set(session, catalog, frame.payload);
        case FrameTag::kExecBatch:
            return handle_batch(session, catalog, frame.payload);
        case FrameTag::kMeta:
        case FrameTag::kRow:
        case FrameTag::kCommandOk:
        case FrameTag::kResultEnd:
        case FrameTag::kTransactionAbort:
        case FrameTag::kError:
        case FrameTag::kPrepareOk:
        case FrameTag::kBatchResult:
            return base::fail(invariant_error(
                "validated client frame selected a server-only tag",
                "dispatch request"
            ));
    }
    return base::fail(
        invariant_error("validated client frame selected an unknown tag", "dispatch request")
    );
}

}  // namespace

struct RequestDispatcher::Impl final {
    explicit Impl(SessionPort& session_port) noexcept
        : session{&session_port} {}

    auto dispatch(const FrameView& frame) -> base::Result<RequestResponse> {
        return dispatch_request(*session, catalog, frame);
    }

    SessionPort* session{nullptr};
    PreparedCatalog catalog;
};

auto RequestDispatcher::create(SessionPort& session) -> base::Result<RequestDispatcher> {
    return base::invoke_exception_boundary(
        kDispatcherExceptionBoundary,
        "create request dispatcher",
        [&]() -> base::Result<RequestDispatcher> {
            return RequestDispatcher{std::make_unique<Impl>(session)};
        }
    );
}

RequestDispatcher::RequestDispatcher(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

RequestDispatcher::RequestDispatcher(RequestDispatcher&& other) noexcept = default;

auto RequestDispatcher::operator=(RequestDispatcher&& other) noexcept
    -> RequestDispatcher& = default;

RequestDispatcher::~RequestDispatcher() = default;

auto RequestDispatcher::valid() const noexcept -> bool {
    return implementation_ != nullptr;
}

auto RequestDispatcher::dispatch(const FrameView& frame) -> base::Result<RequestResponse> {
    if (!valid()) {
        return base::fail(dispatch_error(
            base::ErrorCode::kUnavailable,
            "RMDB request dispatcher has been moved from",
            "dispatch request"
        ));
    }
    return base::invoke_exception_boundary(
        kDispatcherExceptionBoundary,
        "dispatch request",
        [&]() -> base::Result<RequestResponse> {
            return implementation_->dispatch(frame);
        }
    );
}

auto RequestDispatcher::prepared_statement_count() const noexcept -> std::size_t {
    return valid() ? implementation_->catalog.size() : 0U;
}

}  // namespace db::protocol
