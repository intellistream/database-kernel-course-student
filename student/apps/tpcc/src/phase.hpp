#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <vector>

#include "db/base/result.hpp"
#include "db/protocol/rmdb_client.hpp"

#include "config.hpp"
#include "metrics.hpp"
#include "workload.hpp"

namespace db::tpcc {

struct PhaseResult final {
    RoundMetrics metrics;
    TransactionCounters lifetime_counters;
    std::array<std::uint64_t, 50> lifetime_warehouse_completions{};
    SqlTelemetry sql;
};

struct PhaseRequest final {
    const BenchmarkOptions& options;
    const BenchmarkConfig& config;
    std::uint32_t phase_id{0U};
    std::chrono::seconds duration{0};
    const WorkloadRouting& routing;
    const std::vector<protocol::RmdbPreparedStatement>& definitions;
    bool sql_stats{false};
    std::chrono::steady_clock::time_point task_deadline;
};

auto run_phase(const PhaseRequest& request) -> base::Result<PhaseResult>;

auto health_check(
    const BenchmarkOptions& options,
    const BenchmarkConfig& config,
    std::chrono::steady_clock::time_point task_deadline
) -> base::Status;

}  // namespace db::tpcc
