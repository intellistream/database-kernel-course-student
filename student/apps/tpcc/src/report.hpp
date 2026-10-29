#pragma once

#include <cstddef>
#include <ostream>
#include <string_view>
#include <vector>

#include "db/base/result.hpp"

#include "metrics.hpp"
#include "workload.hpp"

namespace db::tpcc {

auto validate_counter_accounting(const TransactionCounters& counters, std::string_view label)
    -> base::Status;
auto validate_round(const RoundMetrics& metrics, const WorkloadRouting& routing) -> base::Status;
auto validate_mix(const TransactionCounters& counters) -> base::Status;
auto validate_combined_coverage(
    const std::vector<RoundMetrics>& rounds,
    const WorkloadRouting& routing
) -> base::Status;

void print_round(std::ostream& output, const RoundMetrics& round, std::size_t number);
void print_round_sql_stats(std::ostream& output, const RoundMetrics& round, std::size_t number);
void print_sql_profile(std::ostream& output, const SqlProfile& profile);

}  // namespace db::tpcc
