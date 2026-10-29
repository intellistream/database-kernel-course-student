#pragma once

#include <iosfwd>

#include "db/base/result.hpp"

#include "config.hpp"
#include "metrics.hpp"

namespace db::tpcc {

auto run_benchmark(const BenchmarkOptions& options, std::ostream& output)
    -> base::Result<BenchmarkResult>;

}  // namespace db::tpcc
