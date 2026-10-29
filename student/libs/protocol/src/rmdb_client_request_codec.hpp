#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"
#include "db/protocol/rmdb_client.hpp"

namespace db::protocol::internal {

// Private payload boundary for requests emitted by RmdbClient. These helpers
// validate the complete client-side contract before any bytes reach a socket.
auto validate_rmdb_sql_request(std::string_view sql, std::string_view operation) -> base::Status;

auto validate_rmdb_prepared_statements(std::span<const RmdbPreparedStatement> statements)
    -> base::Status;

auto encode_rmdb_prepare_set(std::span<const RmdbPreparedStatement> statements)
    -> base::Result<std::vector<std::byte>>;

auto encode_rmdb_batch_request(
    const RmdbPreparedCatalog& catalog,
    std::span<const RmdbBatchOperation> operations
) -> base::Result<std::vector<std::byte>>;

}  // namespace db::protocol::internal
