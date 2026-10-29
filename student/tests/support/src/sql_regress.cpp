#include "db/test_support/sql_regress.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sql_regress_internal.hpp"

namespace db::test_support {

auto detail::regression_error(
    const base::ErrorCode code,
    std::string message,
    std::string operation
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "test_support.sql_regress",
        std::move(operation)
    );
}

namespace {

constexpr std::string_view kScheduleName{"serial_schedule"};

struct ScheduleGroup final {
    std::vector<std::string> names;
};

auto read_text_file(const std::filesystem::path& path) -> base::Result<std::string> {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        return base::fail(
            detail::regression_error(
                base::ErrorCode::kIo,
                "could not read " + path.string(),
                "read fixture"
            )
        );
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        return base::fail(
            detail::regression_error(
                base::ErrorCode::kIo,
                "could not read " + path.string(),
                "read fixture"
            )
        );
    }
    return contents.str();
}

auto selected(const std::vector<std::string>& names, const std::string_view candidate) noexcept
    -> bool {
    return names.empty()
           || std::any_of(names.begin(), names.end(), [candidate](const std::string& name) {
                  return name == candidate;
              });
}

auto parse_schedule_name(const std::string_view name) -> base::Result<std::string> {
    const auto valid = !name.empty() && std::all_of(name.begin(), name.end(), [](const char value) {
        const auto byte = static_cast<unsigned char>(value);
        return std::isalnum(byte) != 0 || value == '_' || value == '-' || value == '.';
    });
    if (!valid) {
        return base::fail(
            detail::regression_error(
                base::ErrorCode::kInvalidArgument,
                "invalid SQL regression test name " + std::string{name},
                "parse SQL schedule"
            )
        );
    }
    return std::string{name};
}

auto parse_schedule_names(std::string_view input) -> base::Result<std::vector<std::string>> {
    std::vector<std::string> names;
    while (true) {
        input = detail::trim_view(input);
        if (input.empty()) {
            return names;
        }
        const auto space = input.find_first_of(" \t");
        auto name = parse_schedule_name(input.substr(0U, space));
        if (!name) {
            return base::fail(std::move(name).error());
        }
        names.push_back(std::move(name).value());
        if (space == std::string_view::npos) {
            return names;
        }
        input.remove_prefix(space);
    }
}

auto parse_schedule_line(std::string_view input) -> base::Result<std::optional<ScheduleGroup>> {
    input = detail::trim_view(input);
    if (input.empty() || input.front() == '#') {
        return std::optional<ScheduleGroup>{};
    }
    if (!input.starts_with("test:")) {
        return base::fail(
            detail::regression_error(
                base::ErrorCode::kInvalidArgument,
                "schedule line must start with `test:`",
                "parse SQL schedule"
            )
        );
    }
    input.remove_prefix(5U);
    auto names = parse_schedule_names(input);
    if (!names) {
        return base::fail(std::move(names).error());
    }
    if (names->empty()) {
        return base::fail(
            detail::regression_error(
                base::ErrorCode::kInvalidArgument,
                "SQL schedule test group is empty",
                "parse SQL schedule"
            )
        );
    }
    return std::optional<ScheduleGroup>{ScheduleGroup{std::move(names).value()}};
}

auto parse_schedule(const std::string_view input) -> base::Result<std::vector<ScheduleGroup>> {
    std::vector<ScheduleGroup> groups;
    std::size_t line_start = 0U;
    while (line_start <= input.size()) {
        const auto line_end = input.find('\n', line_start);
        const auto line = input.substr(
            line_start,
            line_end == std::string_view::npos ? input.size() - line_start : line_end - line_start
        );
        auto group = parse_schedule_line(line);
        if (!group) {
            return base::fail(std::move(group).error());
        }
        if (group->has_value()) {
            groups.push_back(std::move(group).value().value());
        }
        if (line_end == std::string_view::npos) {
            break;
        }
        line_start = line_end + 1U;
    }
    if (groups.empty()) {
        return base::fail(
            detail::regression_error(
                base::ErrorCode::kInvalidArgument,
                "SQL schedule does not contain any tests",
                "parse SQL schedule"
            )
        );
    }
    return groups;
}

}  // namespace

auto run_sql_regressions(const SqlRegressionOptions& options) -> base::Result<SqlRegressionReport> {
    if (options.fixture_root.empty()) {
        return base::fail(
            detail::regression_error(
                base::ErrorCode::kInvalidArgument,
                "SQL regression fixture root is empty",
                "run SQL regressions"
            )
        );
    }
    auto schedule = read_text_file(options.fixture_root / std::filesystem::path{kScheduleName});
    if (!schedule) {
        return base::fail(std::move(schedule).error());
    }
    auto groups = parse_schedule(schedule.value());
    if (!groups) {
        return base::fail(std::move(groups).error());
    }
    SqlRegressionReport report;
    for (const auto& group : groups.value()) {
        for (const auto& name : group.names) {
            if (!selected(options.selected_tests, name)) {
                continue;
            }
            ++report.selected_count;
            auto source = read_text_file(options.fixture_root / "sql" / (name + ".sql"));
            auto expected = read_text_file(options.fixture_root / "expected" / (name + ".out"));
            if (!source || !expected) {
                return base::fail(
                    !source ? std::move(source).error() : std::move(expected).error()
                );
            }
            auto actual =
                detail::execute_fixture_script(source.value(), options.fixture_root, name);
            if (!actual) {
                return base::fail(std::move(actual).error());
            }
            const auto normalized_expected = detail::normalize_output(expected.value());
            const auto normalized_actual = detail::normalize_output(actual.value());
            if (normalized_expected != normalized_actual) {
                report.failures.push_back(
                    SqlRegressionFailure{
                        .name = name,
                        .diff = detail::render_diff(name, normalized_expected, normalized_actual),
                    }
                );
            }
        }
    }
    if (report.selected_count == 0U) {
        return base::fail(
            detail::regression_error(
                base::ErrorCode::kInvalidArgument,
                "SQL regression selection did not select any tests",
                "run SQL regressions"
            )
        );
    }
    return report;
}

}  // namespace db::test_support
