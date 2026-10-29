#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/engine/result.hpp"
#include "db/types/value.hpp"

#include "sql_regress_internal.hpp"

namespace db::test_support::detail {

auto normalize_output(std::string input) -> std::string {
    std::string output;
    output.reserve(input.size() + 1U);
    for (std::size_t index = 0U; index < input.size(); ++index) {
        if (input[index] == '\r') {
            if (index + 1U < input.size() && input[index + 1U] == '\n') {
                ++index;
            }
            output.push_back('\n');
        } else {
            output.push_back(input[index]);
        }
    }
    if (output.empty() || output.back() != '\n') {
        output.push_back('\n');
    }
    return output;
}

namespace {

auto format_value(const types::Value& value) -> std::string {
    if (value.is_null()) {
        return "NULL";
    }
    if (const auto* const boolean = value.as_boolean(); boolean != nullptr) {
        return *boolean ? "t" : "f";
    }
    if (value.as_integer() != nullptr || value.as_float() != nullptr
        || value.as_timestamp() != nullptr) {
        return value.display();
    }
    if (const auto* const string = value.as_string(); string != nullptr) {
        std::string escaped;
        escaped.reserve(string->size());
        for (const char character : *string) {
            switch (character) {
                case '\\':
                    escaped.append("\\\\");
                    break;
                case '\n':
                    escaped.append("\\n");
                    break;
                case '\r':
                    escaped.append("\\r");
                    break;
                case '\t':
                    escaped.append("\\t");
                    break;
                default:
                    escaped.push_back(character);
                    break;
            }
        }
        return escaped;
    }
    return value.display();
}

auto pad_cell(const std::string_view value, const std::size_t width, const bool right_align)
    -> std::string {
    const auto padding = width > value.size() ? width - value.size() : 0U;
    if (right_align) {
        return std::string(padding, ' ') + std::string{value};
    }
    return std::string{value} + std::string(padding, ' ');
}

auto table_column_names(const engine::ResultSet& result_set) -> std::vector<std::string> {
    const auto columns = result_set.schema().columns();
    std::vector<std::string> names;
    names.reserve(columns.size());
    for (const auto& column : columns) {
        names.push_back(column.name);
    }
    return names;
}

auto table_cells(const engine::ResultSet& result_set) -> std::vector<std::vector<std::string>> {
    std::vector<std::vector<std::string>> cells;
    cells.reserve(result_set.size());
    for (const auto& row : result_set.rows()) {
        std::vector<std::string> values;
        values.reserve(row.size());
        for (const auto& value : row.values()) {
            values.push_back(format_value(value));
        }
        cells.push_back(std::move(values));
    }
    return cells;
}

auto normalize_table_shape(
    std::vector<std::string>& names,
    std::vector<std::vector<std::string>>& cells
) -> std::size_t {
    auto column_count = names.size();
    for (const auto& row : cells) {
        column_count = std::max(column_count, row.size());
    }
    while (names.size() < column_count) {
        names.push_back("column_" + std::to_string(names.size() + 1U));
    }
    for (auto& row : cells) {
        while (row.size() < column_count) {
            row.emplace_back();
        }
    }
    return column_count;
}

auto is_numeric_column(const engine::ResultSet& result_set, const std::size_t column) -> bool {
    bool has_value = false;
    for (const auto& row : result_set.rows()) {
        if (column >= row.size()) {
            continue;
        }
        const auto& value = row.values()[column];
        if (value.is_null()) {
            continue;
        }
        has_value = true;
        if (value.as_integer() == nullptr && value.as_float() == nullptr) {
            return false;
        }
    }
    return has_value;
}

auto table_numeric_columns(const engine::ResultSet& result_set, const std::size_t column_count)
    -> std::vector<bool> {
    std::vector<bool> numeric;
    numeric.reserve(column_count);
    for (std::size_t column = 0U; column < column_count; ++column) {
        numeric.push_back(is_numeric_column(result_set, column));
    }
    return numeric;
}

auto table_column_widths(
    const std::vector<std::string>& names,
    const std::vector<std::vector<std::string>>& cells,
    const std::size_t column_count
) -> std::vector<std::size_t> {
    std::vector<std::size_t> widths(column_count, 0U);
    for (std::size_t column = 0U; column < column_count; ++column) {
        widths[column] = names[column].size();
        for (const auto& row : cells) {
            widths[column] = std::max(widths[column], row[column].size());
        }
    }
    return widths;
}

auto format_table_line(
    const std::vector<std::string>& row,
    const std::vector<std::size_t>& widths,
    const std::vector<bool>& numeric
) -> std::string {
    std::string line;
    for (std::size_t column = 0U; column < row.size(); ++column) {
        if (column != 0U) {
            line.append(" | ");
        }
        line.append(pad_cell(row[column], widths[column], numeric[column]));
    }
    return line;
}

auto format_table_separator(const std::vector<std::size_t>& widths) -> std::string {
    std::string separator;
    for (std::size_t column = 0U; column < widths.size(); ++column) {
        if (column != 0U) {
            separator.append("-+-");
        }
        separator.append(std::string(widths[column], '-'));
    }
    return separator;
}

auto format_table_row_count(const std::size_t row_count) -> std::string {
    return "(" + std::to_string(row_count) + (row_count == 1U ? " row)\n\n" : " rows)\n\n");
}

auto error_kind(const base::ErrorCode code) -> std::string {
    switch (code) {
        case base::ErrorCode::kParse:
            return "parse";
        case base::ErrorCode::kBind:
            return "bind";
        case base::ErrorCode::kPlan:
            return "plan";
        case base::ErrorCode::kTransactionState:
            return "transaction";
        case base::ErrorCode::kExecution:
        case base::ErrorCode::kAlreadyExists:
        case base::ErrorCode::kNotFound:
        case base::ErrorCode::kUnsupported:
        case base::ErrorCode::kInvalidArgument:
        case base::ErrorCode::kOutOfRange:
        case base::ErrorCode::kIo:
        case base::ErrorCode::kCorruption:
        case base::ErrorCode::kInvariantViolation:
        case base::ErrorCode::kResourceExhausted:
        case base::ErrorCode::kUnavailable:
        case base::ErrorCode::kCancelled:
        case base::ErrorCode::kInternal:
        case base::ErrorCode::kRebuildRequired:
        case base::ErrorCode::kSerializationFailure:
        case base::ErrorCode::kDeadlockDetected:
            return "exec";
    }
    return "exec";
}

auto split_lines(const std::string_view input) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::size_t start = 0U;
    while (start <= input.size()) {
        const auto end = input.find('\n', start);
        lines.emplace_back(
            input.substr(start, end == std::string_view::npos ? input.size() - start : end - start)
        );
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    if (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

void append_diff_lines(
    std::string& diff,
    const char marker,
    const std::vector<std::string>& lines,
    const std::size_t begin,
    const std::size_t end
) {
    constexpr std::size_t k_maximum_lines = 80U;
    std::size_t emitted = 0U;
    for (std::size_t index = begin; index < end && emitted < k_maximum_lines; ++index, ++emitted) {
        diff.push_back(marker);
        diff.append(lines[index]);
        diff.push_back('\n');
    }
    if (end - begin > k_maximum_lines) {
        diff.append(std::string{marker} + "... diff truncated ...\n");
    }
}

}  // namespace

auto format_table(const engine::ResultSet& result_set) -> std::string {
    auto names = table_column_names(result_set);
    auto cells = table_cells(result_set);
    const auto column_count = normalize_table_shape(names, cells);
    const auto numeric = table_numeric_columns(result_set, column_count);
    const auto widths = table_column_widths(names, cells, column_count);

    std::string output = format_table_line(names, widths, numeric);
    output.push_back('\n');
    output.append(format_table_separator(widths));
    output.push_back('\n');
    for (const auto& row : cells) {
        output.append(format_table_line(row, widths, numeric));
        output.push_back('\n');
    }
    output.append(format_table_row_count(cells.size()));
    return output;
}

auto format_error(const base::Error& error) -> std::string {
    return "ERROR:  " + error_kind(error.code()) + " error: " + std::string{error.message()} + '\n';
}

auto format_transaction_aborted() -> std::string {
    return "ERROR:  current transaction is aborted, commands ignored until end of transaction "
           "block\n";
}

auto render_diff(
    const std::string_view name,
    const std::string_view expected,
    const std::string_view actual
) -> std::string {
    const auto expected_lines = split_lines(expected);
    const auto actual_lines = split_lines(actual);
    std::size_t prefix = 0U;
    while (prefix < expected_lines.size() && prefix < actual_lines.size()
           && expected_lines[prefix] == actual_lines[prefix]) {
        ++prefix;
    }
    std::size_t suffix = 0U;
    while (suffix < expected_lines.size() - prefix && suffix < actual_lines.size() - prefix
           && expected_lines[expected_lines.size() - suffix - 1U]
                  == actual_lines[actual_lines.size() - suffix - 1U]) {
        ++suffix;
    }
    const auto expected_end = expected_lines.size() - suffix;
    const auto actual_end = actual_lines.size() - suffix;
    const auto context_start = prefix > 3U ? prefix - 3U : 0U;
    const auto expected_context_end = std::min(expected_lines.size(), expected_end + 3U);
    const auto actual_context_end = std::min(actual_lines.size(), actual_end + 3U);
    std::string diff =
        std::string{name} + ": first difference at line " + std::to_string(prefix + 1U) + "\n";
    for (std::size_t index = context_start; index < prefix; ++index) {
        diff.push_back(' ');
        diff.append(expected_lines[index]);
        diff.push_back('\n');
    }
    append_diff_lines(diff, '-', expected_lines, prefix, expected_end);
    append_diff_lines(diff, '+', actual_lines, prefix, actual_end);
    const auto context_end =
        std::min(expected_context_end - expected_end, actual_context_end - actual_end);
    for (std::size_t offset = 0U; offset < context_end; ++offset) {
        diff.push_back(' ');
        diff.append(expected_lines[expected_end + offset]);
        diff.push_back('\n');
    }
    return diff;
}

}  // namespace db::test_support::detail
