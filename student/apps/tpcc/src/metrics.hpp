#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "db/protocol/rmdb_client.hpp"

#include "workload.hpp"

namespace db::tpcc {

struct TransactionCounters final {
    std::array<std::uint64_t, 5> attempted{};
    std::array<std::uint64_t, 5> committed{};
    std::array<std::uint64_t, 5> database_aborts{};
    std::array<std::uint64_t, 5> abandoned{};
    std::uint64_t retries{0U};
    std::uint64_t expected_rollbacks{0U};
    std::uint64_t delivery_processed_orders{0U};

    void merge(const TransactionCounters& other) noexcept;
    auto total_committed() const noexcept -> std::uint64_t;
};

struct RoundMetrics final {
    std::uint32_t phase_id{0U};
    std::chrono::seconds duration{0};
    TransactionCounters counters{};
    std::uint32_t covered_warehouses{0U};
    std::uint32_t hot_warehouses_covered{0U};
    std::array<std::uint64_t, 50> completed_by_warehouse{};
    double new_order_per_minute{0.0};
    std::array<std::uint64_t, 5> latency_p50_us{};
    std::array<std::uint64_t, 5> latency_p95_us{};
};

struct StatementMetric final {
    std::uint16_t id{0U};
    const char* name{nullptr};
    std::uint64_t attempted{0U};
    std::uint64_t completed{0U};
    std::uint64_t batch_inclusive_total_ns{0U};
    std::uint64_t attributed_total_ns{0U};
};

struct BatchMetric final {
    const char* name{nullptr};
    std::uint64_t count{0U};
    std::uint64_t total_ns{0U};
    std::uint64_t p50_ns{0U};
    std::uint64_t p95_ns{0U};
};

struct SqlProfile final {
    std::vector<StatementMetric> statements;
    std::vector<BatchMetric> batches;
    std::uint64_t total_batch_count{0U};
    std::uint64_t total_batch_wait_ns{0U};
};

class SqlTelemetry final {
public:
    SqlTelemetry();

    void record(
        std::span<const protocol::RmdbBatchOperation> operations,
        const protocol::RmdbBatchResponse& response,
        std::chrono::nanoseconds elapsed
    );
    void merge(SqlTelemetry other);
    auto into_profile() && -> SqlProfile;

private:
    struct StatementAccumulator final {
        std::uint64_t attempted{0U};
        std::uint64_t completed{0U};
        std::uint64_t batch_inclusive_total_ns{0U};
        std::uint64_t attributed_total_ns{0U};
    };

    struct BatchAccumulator final {
        std::uint64_t count{0U};
        std::uint64_t total_ns{0U};
        std::vector<std::uint64_t> latency_ns;
    };

    std::array<StatementAccumulator, 256> statements_{};
    std::array<BatchAccumulator, 14> batches_{};
};

struct BenchmarkResult final {
    std::uint64_t warmup_committed{0U};
    std::vector<RoundMetrics> rounds;
    double median_new_order_per_minute{0.0};
    std::optional<SqlProfile> sql_profile;
};

}  // namespace db::tpcc
