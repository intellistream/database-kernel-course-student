#include "session.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"

#include "prepared.hpp"

namespace db::tpcc {
namespace {

auto execution_error(std::string message) -> base::detail::Failure {
    return base::fail(base::Error{base::ErrorCode::kExecution, std::move(message)});
}

auto operation(const std::uint16_t statement_id) -> protocol::RmdbBatchOperation {
    return protocol::RmdbBatchOperation{.statement_id = statement_id, .parameters = {}};
}

}  // namespace

SqlSession::SqlSession(
    protocol::RmdbClient& client,
    const protocol::RmdbPreparedCatalog& catalog,
    SqlTelemetry* const telemetry,
    const std::chrono::steady_clock::time_point phase_deadline,
    const std::chrono::steady_clock::time_point cleanup_deadline
) noexcept
    : client_{&client},
      catalog_{&catalog},
      telemetry_{telemetry},
      phase_deadline_{phase_deadline},
      cleanup_deadline_{cleanup_deadline} {}

auto SqlSession::batch(const std::span<const protocol::RmdbBatchOperation> operations)
    -> base::Result<std::optional<protocol::RmdbBatchResponse>> {
    const auto started_at = std::chrono::steady_clock::now();
    if (started_at >= phase_deadline_) {
        if (transaction_open_) {
            auto aborted = abort_cleanup();
            if (!aborted) {
                return base::fail(std::move(aborted).error());
            }
        }
        deadline_stopped_ = true;
        return std::optional<protocol::RmdbBatchResponse>{};
    }
    if (started_at >= cleanup_deadline_) {
        return execution_error("TPC-C phase grace period expired before SQL I/O");
    }

    auto response = client_->execute_batch(*catalog_, operations);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    const auto completed_at = std::chrono::steady_clock::now();
    if (telemetry_ != nullptr && completed_at <= phase_deadline_) {
        telemetry_->record(
            operations,
            response.value(),
            std::chrono::duration_cast<std::chrono::nanoseconds>(completed_at - started_at)
        );
    }

    switch (response->terminal.kind) {
        case protocol::RmdbBatchTerminalKind::kOk:
            if (response->executed_operation_count != operations.size()) {
                return execution_error(
                    "successful TPC-C batch did not execute every requested operation"
                );
            }
            if (!operations.empty() && operations.front().statement_id == statement::kBegin) {
                transaction_open_ = true;
            }
            if (std::ranges::any_of(operations, [](const auto& current) {
                    return current.statement_id == statement::kCommit
                           || current.statement_id == statement::kAbort;
                })) {
                transaction_open_ = false;
            }
            return std::optional<protocol::RmdbBatchResponse>{std::move(response).value()};
        case protocol::RmdbBatchTerminalKind::kTransactionAbort:
            transaction_open_ = false;
            return std::optional<protocol::RmdbBatchResponse>{};
        case protocol::RmdbBatchTerminalKind::kError:
            transaction_open_ = false;
            return execution_error(
                "TPC-C prepared batch failed at operation "
                + std::to_string(response->terminal.failed_operation) + ": "
                + response->terminal.diagnostic
            );
    }
    return execution_error("TPC-C batch returned an unknown terminal");
}

auto SqlSession::abort_open_transaction() -> base::Status {
    const std::vector<protocol::RmdbBatchOperation> operations{operation(statement::kAbort)};
    auto response = batch(operations);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (deadline_stopped_) {
        return {};
    }
    if (!response->has_value()) {
        return execution_error("TPC-C ABORT itself returned TRANSACTION_ABORT");
    }
    return {};
}

auto SqlSession::deadline_stopped() const noexcept -> bool {
    return deadline_stopped_;
}

auto SqlSession::abort_cleanup() -> base::Status {
    if (std::chrono::steady_clock::now() >= cleanup_deadline_) {
        return execution_error("TPC-C phase grace period expired before cleanup ABORT");
    }
    const std::vector<protocol::RmdbBatchOperation> operations{operation(statement::kAbort)};
    auto response = client_->execute_batch(*catalog_, operations);
    transaction_open_ = false;
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (response->terminal.kind == protocol::RmdbBatchTerminalKind::kError) {
        return execution_error(
            "TPC-C deadline cleanup ABORT failed: " + response->terminal.diagnostic
        );
    }
    return {};
}

}  // namespace db::tpcc
