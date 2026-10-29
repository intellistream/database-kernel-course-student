#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/api/database.hpp"
#include "db/api/result.hpp"
#include "db/base/error.hpp"
#include "db/base/result.hpp"
#include "db/protocol/session_port.hpp"
#include "db/types/data_type.hpp"

namespace db::server::internal {

auto api_adapter_error(base::ErrorCode code, std::string message, std::string_view operation)
    -> base::Error;

auto api_parameter_data_type(protocol::WireDataType data_type) -> base::Result<types::DataType>;

auto map_api_columns(std::span<const api::ResultColumn> columns)
    -> base::Result<std::vector<protocol::ColumnMetadata>>;

auto map_api_dispatch_result(std::string_view stream_sql, api::DispatchResult dispatch)
    -> base::Result<protocol::ExecutionResult>;

}  // namespace db::server::internal
