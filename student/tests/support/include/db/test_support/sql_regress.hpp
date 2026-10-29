#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "db/base/result.hpp"

namespace db::test_support {

struct SqlRegressionOptions final {
    std::filesystem::path fixture_root;
    std::vector<std::string> selected_tests;
};

struct SqlRegressionFailure final {
    std::string name;
    std::string diff;
};

struct SqlRegressionReport final {
    std::size_t selected_count{0U};
    std::vector<SqlRegressionFailure> failures;

    auto passed() const noexcept -> bool {
        return failures.empty();
    }
};

// Runs the fixed SQL regression schedule against an isolated durable C++
// database for each case. The harness owns only test concerns: script
// splitting, golden formatting, and the fixture-only COPY adapter. Product
// SQL parsing, binding, planning, execution, and transaction state remain in
// the public engine facade.
auto run_sql_regressions(const SqlRegressionOptions& options) -> base::Result<SqlRegressionReport>;

}  // namespace db::test_support
