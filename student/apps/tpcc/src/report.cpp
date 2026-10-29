#include "report.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <string>

#include "db/base/error.hpp"

#include "array_access.hpp"

namespace db::tpcc {
namespace {

inline constexpr std::uint64_t kFullCoverageSampleCompletions = 400U;
inline constexpr std::array<std::uint32_t, 5> kExpectedMix{45U, 43U, 4U, 4U, 4U};

auto benchmark_error(std::string message) -> base::detail::Failure {
    return base::fail(base::Error{base::ErrorCode::kExecution, std::move(message)});
}

void merge_coverage(
    std::array<std::uint64_t, 50>& target,
    const std::array<std::uint64_t, 50>& source
) noexcept {
    for (std::size_t index = 0U; index < target.size(); ++index) {
        array_element(target, index) += array_element(source, index);
    }
}

auto total(const std::array<std::uint64_t, 5>& counters) noexcept -> std::uint64_t {
    std::uint64_t result = 0U;
    for (const std::uint64_t value : counters) {
        result += value;
    }
    return result;
}

auto coverage_total(const std::array<std::uint64_t, 50>& counters) noexcept -> std::uint64_t {
    std::uint64_t result = 0U;
    for (const std::uint64_t value : counters) {
        result += value;
    }
    return result;
}

auto scaled_coverage_requirement(
    const std::uint32_t full_requirement,
    const std::uint64_t completed
) noexcept -> std::uint32_t {
    if (completed == 0U) {
        return 0U;
    }
    const std::uint64_t sample = std::min(completed, kFullCoverageSampleCompletions);
    return static_cast<std::uint32_t>(
        (std::uint64_t{full_requirement} * sample + kFullCoverageSampleCompletions - 1U)
        / kFullCoverageSampleCompletions
    );
}

auto mix_tolerance(const std::size_t index, const bool full_sample) noexcept -> double {
    if (index < 2U) {
        return full_sample ? 2.0 : 3.0;
    }
    return full_sample ? 1.0 : 1.5;
}

auto percentage(const std::uint64_t value, const std::uint64_t total_value) noexcept -> double {
    return total_value == 0U
               ? 0.0
               : static_cast<double>(value) * 100.0 / static_cast<double>(total_value);
}

auto average_us(const std::uint64_t total_ns, const std::uint64_t count) noexcept -> double {
    return count == 0U ? 0.0 : static_cast<double>(total_ns) / static_cast<double>(count) / 1'000.0;
}

}  // namespace

auto validate_counter_accounting(const TransactionCounters& counters, const std::string_view label)
    -> base::Status {
    if (counters.committed.front() + counters.expected_rollbacks + counters.abandoned.front()
        != counters.attempted.front()) {
        return benchmark_error(std::string{label}.append(" NewOrder counters do not reconcile"));
    }
    for (std::size_t index = 1U; index < counters.attempted.size(); ++index) {
        if (array_element(counters.committed, index) + array_element(counters.abandoned, index)
            != array_element(counters.attempted, index)) {
            return benchmark_error(
                std::string{label}.append(" transaction counters do not reconcile")
            );
        }
    }
    if (total(counters.database_aborts) != counters.retries + total(counters.abandoned)) {
        return benchmark_error(std::string{label}.append(" retry counters do not reconcile"));
    }
    return {};
}

auto validate_round(const RoundMetrics& metrics, const WorkloadRouting& routing) -> base::Status {
    const std::string label = "TPC-C ranked phase " + std::to_string(metrics.phase_id);
    auto accounting = validate_counter_accounting(metrics.counters, label);
    if (!accounting) {
        return base::fail(std::move(accounting).error());
    }
    for (const TransactionKind kind : kTransactionKinds) {
        if (array_element(metrics.counters.committed, transaction_index(kind)) == 0U) {
            return benchmark_error(label + " committed no " + transaction_name(kind));
        }
    }
    if (metrics.counters.delivery_processed_orders == 0U) {
        return benchmark_error(label + " delivery transactions processed no queued order");
    }
    const std::uint64_t completed = coverage_total(metrics.completed_by_warehouse);
    if (metrics.counters.total_committed() + metrics.counters.expected_rollbacks != completed) {
        return benchmark_error(label + " warehouse coverage counters do not reconcile");
    }
    const std::uint32_t required = scaled_coverage_requirement(45U, completed);
    if (metrics.covered_warehouses < required) {
        return benchmark_error(label + " warehouse coverage is below the ranked requirement");
    }
    if (completed >= kFullCoverageSampleCompletions
        && metrics.hot_warehouses_covered != routing.hot_warehouses().size()) {
        return benchmark_error(label + " did not cover every hot warehouse");
    }
    return {};
}

auto validate_mix(const TransactionCounters& counters) -> base::Status {
    const std::uint64_t attempted = total(counters.attempted);
    if (attempted < 2'000U) {
        return {};
    }
    const bool full_sample = attempted >= 10'000U;
    for (std::size_t index = 0U; index < counters.attempted.size(); ++index) {
        const double observed = static_cast<double>(array_element(counters.attempted, index))
                                * 100.0 / static_cast<double>(attempted);
        const double expected = static_cast<double>(array_element(kExpectedMix, index));
        const double tolerance = mix_tolerance(index, full_sample);
        if (observed < expected - tolerance || observed > expected + tolerance) {
            return benchmark_error("TPC-C lifetime transaction mix is outside tolerance");
        }
    }
    return {};
}

auto validate_combined_coverage(
    const std::vector<RoundMetrics>& rounds,
    const WorkloadRouting& routing
) -> base::Status {
    std::array<std::uint64_t, 50> combined{};
    for (const RoundMetrics& round : rounds) {
        merge_coverage(combined, round.completed_by_warehouse);
    }
    const std::uint64_t completed = coverage_total(combined);
    const auto covered =
        static_cast<std::uint32_t>(std::ranges::count_if(combined, [](const std::uint64_t count) {
            return count > 0U;
        }));
    if (covered < scaled_coverage_requirement(50U, completed)) {
        return benchmark_error("combined TPC-C ranked rounds did not cover enough warehouses");
    }
    if (completed >= kFullCoverageSampleCompletions) {
        for (const std::uint32_t warehouse : routing.hot_warehouses()) {
            if (array_element(combined, warehouse - 1U) == 0U) {
                return benchmark_error(
                    "combined TPC-C ranked rounds did not cover every hot warehouse"
                );
            }
        }
    }
    return {};
}

void print_round(std::ostream& output, const RoundMetrics& round, const std::size_t number) {
    output << std::fixed << std::setprecision(2) << "[pass] TPC-C round=" << number << "/3"
           << " new_order_per_min=" << round.new_order_per_minute
           << " committed=" << round.counters.total_committed()
           << " coverage=" << round.covered_warehouses << "/50\n";
}

void print_round_sql_stats(
    std::ostream& output,
    const RoundMetrics& round,
    const std::size_t number
) {
    for (const TransactionKind kind : kTransactionKinds) {
        const std::size_t index = transaction_index(kind);
        output << "[sql-stats] transaction round=" << number << " kind=" << transaction_name(kind)
               << " p50_us=" << array_element(round.latency_p50_us, index)
               << " p95_us=" << array_element(round.latency_p95_us, index) << '\n';
        output << "[sql-stats] counters round=" << number << " kind=" << transaction_name(kind)
               << " attempted=" << array_element(round.counters.attempted, index)
               << " committed=" << array_element(round.counters.committed, index)
               << " database_aborts=" << array_element(round.counters.database_aborts, index)
               << '\n';
    }
    output << "[sql-stats] counters round=" << number
           << " expected_rollbacks=" << round.counters.expected_rollbacks << '\n';
}

void print_sql_profile(std::ostream& output, const SqlProfile& profile) {
    output << std::fixed << std::setprecision(3)
           << "[sql-stats] scope=ranked_windows batches=" << profile.total_batch_count
           << " batch_wait_ms=" << static_cast<double>(profile.total_batch_wait_ns) / 1'000'000.0
           << " statement_latency=batch_inclusive attribution=equal_share\n";
    for (const BatchMetric& batch : profile.batches) {
        output << "[sql-stats] batch name=" << batch.name << " count=" << batch.count
               << " total_ms=" << static_cast<double>(batch.total_ns) / 1'000'000.0
               << " share_pct=" << percentage(batch.total_ns, profile.total_batch_wait_ns)
               << " avg_us=" << average_us(batch.total_ns, batch.count)
               << " p50_us=" << static_cast<double>(batch.p50_ns) / 1'000.0
               << " p95_us=" << static_cast<double>(batch.p95_ns) / 1'000.0 << '\n';
    }
    for (const StatementMetric& statement : profile.statements) {
        output << "[sql-stats] statement id=" << statement.id << " name=" << statement.name
               << " attempted=" << statement.attempted << " completed=" << statement.completed
               << " attributed_total_ms="
               << static_cast<double>(statement.attributed_total_ns) / 1'000'000.0
               << " attributed_share_pct="
               << percentage(statement.attributed_total_ns, profile.total_batch_wait_ns)
               << " attributed_avg_us="
               << average_us(statement.attributed_total_ns, statement.attempted)
               << " batch_inclusive_avg_us="
               << average_us(statement.batch_inclusive_total_ns, statement.attempted) << '\n';
    }
}

}  // namespace db::tpcc
