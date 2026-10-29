#include "runner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <utility>
#include <vector>

#include "array_access.hpp"
#include "load.hpp"
#include "phase.hpp"
#include "prepared.hpp"
#include "report.hpp"
#include "workload.hpp"

namespace db::tpcc {

using Clock = std::chrono::steady_clock;

auto run_benchmark(const BenchmarkOptions& options, std::ostream& output)
    -> base::Result<BenchmarkResult> {
    const BenchmarkConfig config = BenchmarkConfig::for_profile(options.profile);
    const Clock::time_point task_deadline = Clock::now() + config.performance_timeout;
    auto loaded = run_load_phase(options, config, task_deadline, output);
    if (!loaded) {
        return base::fail(std::move(loaded).error());
    }

    const WorkloadRouting routing = WorkloadRouting::derive(options.seed);
    const std::vector<protocol::RmdbPreparedStatement> definitions = statement::definitions();
    output << "[stage] TPC-C ranked clients=" << config.clients
           << " warmup=" << config.warmup.count() << "s rounds=" << config.ranked_rounds << 'x'
           << config.ranked_duration.count() << "s seed=" << options.seed << '\n';
    auto initial_health = health_check(options, config, task_deadline);
    if (!initial_health) {
        return base::fail(std::move(initial_health).error());
    }

    auto warmup = run_phase(
        PhaseRequest{
            .options = options,
            .config = config,
            .phase_id = 1U,
            .duration = config.warmup,
            .routing = routing,
            .definitions = definitions,
            .sql_stats = false,
            .task_deadline = task_deadline,
        }
    );
    if (!warmup) {
        return base::fail(std::move(warmup).error());
    }
    auto warmup_health = health_check(options, config, task_deadline);
    if (!warmup_health) {
        return base::fail(std::move(warmup_health).error());
    }

    BenchmarkResult result;
    result.warmup_committed = warmup->lifetime_counters.total_committed();
    result.rounds.reserve(config.ranked_rounds);
    TransactionCounters lifetime = warmup->lifetime_counters;
    SqlTelemetry ranked_sql;
    for (std::size_t round_number = 0U; round_number < config.ranked_rounds; ++round_number) {
        auto phase = run_phase(
            PhaseRequest{
                .options = options,
                .config = config,
                .phase_id = static_cast<std::uint32_t>(round_number + 2U),
                .duration = config.ranked_duration,
                .routing = routing,
                .definitions = definitions,
                .sql_stats = options.sql_stats,
                .task_deadline = task_deadline,
            }
        );
        if (!phase) {
            return base::fail(std::move(phase).error());
        }
        auto phase_health = health_check(options, config, task_deadline);
        if (!phase_health) {
            return base::fail(std::move(phase_health).error());
        }
        auto valid = validate_round(phase->metrics, routing);
        if (!valid) {
            return base::fail(std::move(valid).error());
        }
        lifetime.merge(phase->lifetime_counters);
        ranked_sql.merge(std::move(phase->sql));
        result.rounds.push_back(phase->metrics);
        print_round(output, result.rounds.back(), round_number + 1U);
        if (options.sql_stats) {
            print_round_sql_stats(output, result.rounds.back(), round_number + 1U);
        }
    }
    auto coverage = validate_combined_coverage(result.rounds, routing);
    if (!coverage) {
        return base::fail(std::move(coverage).error());
    }
    auto accounting = validate_counter_accounting(lifetime, "TPC-C ranked benchmark lifetime");
    if (!accounting) {
        return base::fail(std::move(accounting).error());
    }
    auto mix = validate_mix(lifetime);
    if (!mix) {
        return base::fail(std::move(mix).error());
    }

    std::array<double, 3> throughput{
        result.rounds[0U].new_order_per_minute,
        result.rounds[1U].new_order_per_minute,
        result.rounds[2U].new_order_per_minute,
    };
    std::sort(throughput.begin(), throughput.end());
    result.median_new_order_per_minute = array_element(throughput, 1U);
    output << std::fixed << std::setprecision(2)
           << "[pass] TPC-C warmup_committed=" << result.warmup_committed
           << " median_new_order_per_min=" << result.median_new_order_per_minute << '\n';
    if (options.sql_stats) {
        result.sql_profile = std::move(ranked_sql).into_profile();
        print_sql_profile(output, *result.sql_profile);
    }
    return result;
}

}  // namespace db::tpcc
