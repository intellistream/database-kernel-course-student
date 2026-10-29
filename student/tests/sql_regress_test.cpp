#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "db/test_support/sql_regress.hpp"

#ifndef DB_SQL_REGRESS_ROOT
    #error "DB_SQL_REGRESS_ROOT must point at the checked-in SQL regression fixtures"
#endif

namespace {

auto selected_tests_from_environment() -> std::vector<std::string> {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): CTest supplies an immutable launch environment.
    const auto* const value = std::getenv("DB_SQL_TEST");
    if (value == nullptr) {
        return {};
    }
    std::vector<std::string> names;
    std::string current;
    for (const char character : std::string{value}) {
        if (character == ',' || character == ' ' || character == '\t') {
            if (!current.empty()) {
                names.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(character);
        }
    }
    if (!current.empty()) {
        names.push_back(std::move(current));
    }
    return names;
}

}  // namespace

TEST(SqlRegressionTest, ScheduleMatchesCheckedInGoldenOutputs) {
    const db::test_support::SqlRegressionOptions options{
        .fixture_root = std::filesystem::path{DB_SQL_REGRESS_ROOT},
        .selected_tests = selected_tests_from_environment(),
    };
    auto report = db::test_support::run_sql_regressions(options);
    ASSERT_TRUE(report) << report.error().to_string();
    if (!report->passed()) {
        std::string message;
        for (const auto& failure : report->failures) {
            message.append(failure.diff);
        }
        FAIL() << report->failures.size() << " SQL regression case(s) failed\n" << message;
    }
    if (options.selected_tests.empty()) {
        EXPECT_EQ(report->selected_count, 11U);
    } else {
        EXPECT_EQ(report->selected_count, options.selected_tests.size());
    }
}
