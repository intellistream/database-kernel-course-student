#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string_view>

#include "db/base/result.hpp"

#include "config.hpp"

namespace db::tpcc {

struct LoadTableSource final {
    std::string_view table;
    std::filesystem::path csv_path;
    std::uint64_t row_count{0U};
};

struct LoadDatasetSource final {
    std::filesystem::path root;
    std::array<LoadTableSource, 9> tables;

    auto total_rows() const noexcept -> std::uint64_t;
};

// Validates one complete grader dataset-cache manifest before any schema
// mutation. The returned absolute paths and row counts are trusted by the load
// executor; the server remains responsible for CSV syntax and typed decoding.
auto inspect_load_dataset(const std::filesystem::path& directory, const DatasetScale& scale)
    -> base::Result<LoadDatasetSource>;

auto run_load_phase(
    const BenchmarkOptions& options,
    const BenchmarkConfig& config,
    std::chrono::steady_clock::time_point task_deadline,
    std::ostream& output
) -> base::Status;

}  // namespace db::tpcc
