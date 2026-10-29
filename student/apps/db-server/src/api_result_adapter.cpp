#include "api_result_adapter.hpp"

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/text.hpp"
#include "db/types/value.hpp"

#include "stream_sql_compatibility.hpp"

namespace db::server::internal {

auto api_adapter_error(
    const base::ErrorCode code,
    std::string message,
    const std::string_view operation
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "apps.db_server.adapter",
        std::string{operation}
    );
}

auto api_parameter_data_type(const protocol::WireDataType data_type)
    -> base::Result<types::DataType> {
    const types::DataType mapped = protocol::wire_value_data_type(data_type);
    if (mapped.id() != types::kUnknownTypeId) {
        return mapped;
    }
    return base::fail(api_adapter_error(
        base::ErrorCode::kInvalidArgument,
        "RMDB request declared an unknown parameter type",
        "map parameter type"
    ));
}

namespace {

auto wire_data_type(const types::DataType data_type) -> base::Result<protocol::WireDataType> {
    if (data_type.id() == types::kInt64TypeId) {
        return protocol::WireDataType::kInt32;
    }
    if (data_type.id() == types::kFloat32TypeId) {
        return protocol::WireDataType::kFloat32;
    }
    if (data_type.id() == types::kCharTypeId || data_type.id() == types::kTimestampTypeId) {
        return protocol::WireDataType::kChar;
    }
    return base::fail(api_adapter_error(
        base::ErrorCode::kUnsupported,
        "database API result type cannot be represented by RMDB v3",
        "map result type"
    ));
}

auto validate_utf8_text(
    const std::string_view text,
    const std::string_view field,
    const bool require_nonempty
) -> base::Status {
    if (require_nonempty && text.empty()) {
        return base::fail(api_adapter_error(
            base::ErrorCode::kExecution,
            "database API produced an empty " + std::string{field},
            "validate result text"
        ));
    }
    if (!base::is_valid_utf8(text)) {
        return base::fail(api_adapter_error(
            base::ErrorCode::kExecution,
            "database API produced non-UTF-8 " + std::string{field},
            "validate result text"
        ));
    }
    return {};
}

}  // namespace

auto map_api_columns(const std::span<const api::ResultColumn> columns)
    -> base::Result<std::vector<protocol::ColumnMetadata>> {
    std::vector<protocol::ColumnMetadata> result;
    result.reserve(columns.size());
    for (const api::ResultColumn& column : columns) {
        auto valid_name = validate_utf8_text(column.name, "result column name", true);
        if (!valid_name) {
            return base::fail(std::move(valid_name).error());
        }
        auto mapped_type = wire_data_type(column.data_type);
        if (!mapped_type) {
            return base::fail(std::move(mapped_type).error());
        }
        result.push_back(
            protocol::ColumnMetadata{
                .name = column.name,
                .data_type = mapped_type.value(),
            }
        );
    }
    return result;
}

auto map_api_dispatch_result(const std::string_view stream_sql, api::DispatchResult dispatch)
    -> base::Result<protocol::ExecutionResult> {
    if (dispatch.statement_count != 1U) {
        return base::fail(api_adapter_error(
            base::ErrorCode::kUnsupported,
            "RMDB EXEC_STREAM accepts exactly one SQL statement",
            "map execution result"
        ));
    }
    if (dispatch.result_sets.empty()) {
        return protocol::ExecutionResult{};
    }
    if (dispatch.result_sets.size() != 1U) {
        return base::fail(api_adapter_error(
            base::ErrorCode::kUnsupported,
            "RMDB EXEC_STREAM cannot encode multiple SQL result sets",
            "map execution result"
        ));
    }

    api::ResultSet& result_set = dispatch.result_sets.front();
    auto columns = map_api_columns(result_set.schema().columns());
    if (!columns) {
        return base::fail(std::move(columns).error());
    }
    if (columns->empty()) {
        return base::fail(api_adapter_error(
            base::ErrorCode::kExecution,
            "database API query result has no RMDB-visible columns",
            "map execution result"
        ));
    }

    // ResultSet already established row widths and types. Transfer its owned
    // values; the protocol still validates representability in the wire domain.
    auto data = std::move(result_set).into_data();
    if (!data) {
        return base::fail(std::move(data).error());
    }
    if (!stream_sql.empty()) {
        normalize_empty_stream_sums(stream_sql, columns.value(), data->rows);
    }
    return protocol::ExecutionResult::query(std::move(columns).value(), std::move(data->rows));
}

}  // namespace db::server::internal
