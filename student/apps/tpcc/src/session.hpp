#pragma once

#include <chrono>
#include <optional>
#include <span>

#include "db/base/result.hpp"
#include "db/protocol/rmdb_client.hpp"

#include "metrics.hpp"

namespace db::tpcc {

class SqlSession final {
public:
    SqlSession(
        protocol::RmdbClient& client,
        const protocol::RmdbPreparedCatalog& catalog,
        SqlTelemetry* telemetry,
        std::chrono::steady_clock::time_point phase_deadline,
        std::chrono::steady_clock::time_point cleanup_deadline
    ) noexcept;

    auto batch(std::span<const protocol::RmdbBatchOperation> operations)
        -> base::Result<std::optional<protocol::RmdbBatchResponse>>;
    auto abort_open_transaction() -> base::Status;
    auto deadline_stopped() const noexcept -> bool;

private:
    auto abort_cleanup() -> base::Status;

    protocol::RmdbClient* client_;
    const protocol::RmdbPreparedCatalog* catalog_;
    SqlTelemetry* telemetry_;
    std::chrono::steady_clock::time_point phase_deadline_;
    std::chrono::steady_clock::time_point cleanup_deadline_;
    bool transaction_open_{false};
    bool deadline_stopped_{false};
};

}  // namespace db::tpcc
