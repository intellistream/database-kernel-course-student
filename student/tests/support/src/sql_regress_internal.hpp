#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "db/base/error.hpp"
#include "db/base/result.hpp"

namespace db::engine {
class ResultSet;
}  // namespace db::engine

namespace db::test_support::detail {

inline auto is_ascii_space(const char value) noexcept -> bool {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f'
           || value == '\v';
}

inline auto trim_view(std::string_view input) noexcept -> std::string_view {
    while (!input.empty() && is_ascii_space(input.front())) {
        input.remove_prefix(1U);
    }
    while (!input.empty() && is_ascii_space(input.back())) {
        input.remove_suffix(1U);
    }
    return input;
}

auto regression_error(base::ErrorCode code, std::string message, std::string operation)
    -> base::Error;

auto normalize_output(std::string input) -> std::string;
auto format_table(const engine::ResultSet& result_set) -> std::string;
auto format_error(const base::Error& error) -> std::string;
auto format_transaction_aborted() -> std::string;
auto render_diff(std::string_view name, std::string_view expected, std::string_view actual)
    -> std::string;

auto execute_fixture_script(
    std::string_view source,
    const std::filesystem::path& fixture_root,
    std::string_view name
) -> base::Result<std::string>;

}  // namespace db::test_support::detail
