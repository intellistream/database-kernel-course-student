#include "stream_sql_compatibility.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/text.hpp"

namespace db::server::internal {
namespace {

auto compatibility_error(
    const base::ErrorCode code,
    std::string message,
    const std::string_view operation
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "apps.db_server.compatibility",
        std::string{operation}
    );
}

auto trim_statement_terminator(std::string_view sql) noexcept -> std::string_view {
    sql = base::trim_ascii_space(sql);
    if (!sql.empty() && sql.back() == ';') {
        sql.remove_suffix(1U);
    }
    return base::trim_ascii_space(sql);
}

auto normalized_words(const std::string_view sql) -> std::string {
    std::string result;
    bool previous_was_space = false;
    for (const char character : trim_statement_terminator(sql)) {
        if (base::is_ascii_space(character)) {
            previous_was_space = true;
            continue;
        }
        if (previous_was_space && !result.empty()) {
            result.push_back(' ');
        }
        result.push_back(base::ascii_lower(character));
        previous_was_space = false;
    }
    return result;
}

auto first_keyword(const std::string_view sql) noexcept -> std::string_view {
    std::string_view trimmed = base::trim_ascii_space(sql);
    std::size_t length = 0U;
    while (length < trimmed.size() && !base::is_ascii_space(trimmed[length])
           && trimmed[length] != ';') {
        ++length;
    }
    return trimmed.substr(0U, length);
}

auto is_simple_identifier(const std::string_view text) noexcept -> bool {
    if (text.empty()) {
        return false;
    }
    const auto alpha_or_underscore = [](const char character) noexcept {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
               || character == '_';
    };
    const auto alphanumeric_or_underscore = [alpha_or_underscore](const char character) noexcept {
        return alpha_or_underscore(character) || (character >= '0' && character <= '9');
    };
    return alpha_or_underscore(text.front())
           && std::ranges::all_of(text.substr(1U), alphanumeric_or_underscore);
}

auto ascii_starts_with(const std::string_view text, const std::string_view prefix) noexcept
    -> bool {
    return text.size() >= prefix.size()
           && base::ascii_equal_ignoring_case(text.substr(0U, prefix.size()), prefix);
}

auto has_word_on_clause(const std::string_view text) noexcept -> bool {
    for (std::size_t index = 0U; index + 2U <= text.size(); ++index) {
        if (!base::ascii_equal_ignoring_case(text.substr(index, 2U), "on")) {
            continue;
        }
        const bool left_boundary = index == 0U || base::is_ascii_space(text[index - 1U]);
        const bool right_boundary =
            index + 2U == text.size() || base::is_ascii_space(text[index + 2U]);
        if (left_boundary && right_boundary) {
            return true;
        }
    }
    return false;
}

auto ascii_identifier_character(const char character) noexcept -> bool {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
           || (character >= '0' && character <= '9') || character == '_';
}

auto keyword_at(
    const std::string_view text,
    const std::size_t offset,
    const std::string_view keyword
) noexcept -> bool {
    if (offset + keyword.size() > text.size()) {
        return false;
    }
    const bool left_boundary = offset == 0U || !ascii_identifier_character(text[offset - 1U]);
    const bool right_boundary = offset + keyword.size() == text.size()
                                || !ascii_identifier_character(text[offset + keyword.size()]);
    return left_boundary && right_boundary
           && base::ascii_equal_ignoring_case(text.substr(offset, keyword.size()), keyword);
}

enum class CompatibilityScanState : std::uint8_t {
    kNormal,
    kSingleQuoted,
    kDoubleQuoted,
    kLineComment,
    kBlockComment,
};

void advance_compatibility_scan(
    const std::string_view text,
    std::size_t& offset,
    CompatibilityScanState& state
) noexcept {
    const char current = text[offset];
    const char next = offset + 1U < text.size() ? text[offset + 1U] : '\0';
    switch (state) {
        case CompatibilityScanState::kSingleQuoted:
            if (current == '\'' && next == '\'') {
                offset += 2U;
                return;
            }
            if (current == '\'') {
                state = CompatibilityScanState::kNormal;
            }
            ++offset;
            return;
        case CompatibilityScanState::kDoubleQuoted:
            if (current == '"' && next == '"') {
                offset += 2U;
                return;
            }
            if (current == '"') {
                state = CompatibilityScanState::kNormal;
            }
            ++offset;
            return;
        case CompatibilityScanState::kLineComment:
            if (current == '\n' || current == '\r') {
                state = CompatibilityScanState::kNormal;
            }
            ++offset;
            return;
        case CompatibilityScanState::kBlockComment:
            if (current == '*' && next == '/') {
                state = CompatibilityScanState::kNormal;
                offset += 2U;
                return;
            }
            ++offset;
            return;
        case CompatibilityScanState::kNormal:
            return;
    }
}

auto begin_compatibility_non_normal(
    const std::string_view text,
    std::size_t& offset,
    CompatibilityScanState& state
) noexcept -> bool {
    const char current = text[offset];
    const char next = offset + 1U < text.size() ? text[offset + 1U] : '\0';
    if (current == '\'') {
        state = CompatibilityScanState::kSingleQuoted;
        ++offset;
        return true;
    }
    if (current == '"') {
        state = CompatibilityScanState::kDoubleQuoted;
        ++offset;
        return true;
    }
    if (current == '-' && next == '-') {
        state = CompatibilityScanState::kLineComment;
        offset += 2U;
        return true;
    }
    if (current == '/' && next == '*') {
        state = CompatibilityScanState::kBlockComment;
        offset += 2U;
        return true;
    }
    return false;
}

auto contains_top_level_group_by(const std::string_view sql) noexcept -> bool {
    CompatibilityScanState state{CompatibilityScanState::kNormal};
    std::size_t depth{0U};
    std::size_t offset{0U};
    while (offset < sql.size()) {
        if (state != CompatibilityScanState::kNormal) {
            advance_compatibility_scan(sql, offset, state);
            continue;
        }
        if (begin_compatibility_non_normal(sql, offset, state)) {
            continue;
        }
        if (sql[offset] == '(') {
            ++depth;
            ++offset;
            continue;
        }
        if (sql[offset] == ')') {
            if (depth > 0U) {
                --depth;
            }
            ++offset;
            continue;
        }
        if (depth == 0U && keyword_at(sql, offset, "group")) {
            std::size_t after_group = offset + 5U;
            while (after_group < sql.size() && base::is_ascii_space(sql[after_group])) {
                ++after_group;
            }
            if (keyword_at(sql, after_group, "by")) {
                return true;
            }
        }
        ++offset;
    }
    return false;
}

auto top_level_select_items(const std::string_view sql)
    -> std::optional<std::vector<std::string_view>> {
    constexpr std::string_view k_select = "select";
    const std::string_view statement = trim_statement_terminator(sql);
    if (!ascii_starts_with(statement, k_select)
        || (statement.size() > k_select.size()
            && ascii_identifier_character(statement[k_select.size()]))) {
        return std::nullopt;
    }

    std::vector<std::string_view> items;
    CompatibilityScanState state{CompatibilityScanState::kNormal};
    std::size_t depth{0U};
    std::size_t item_begin{k_select.size()};
    std::size_t offset{item_begin};
    while (offset < statement.size()) {
        if (state != CompatibilityScanState::kNormal) {
            advance_compatibility_scan(statement, offset, state);
            continue;
        }
        if (begin_compatibility_non_normal(statement, offset, state)) {
            continue;
        }
        if (statement[offset] == '(') {
            ++depth;
            ++offset;
            continue;
        }
        if (statement[offset] == ')') {
            if (depth == 0U) {
                return std::nullopt;
            }
            --depth;
            ++offset;
            continue;
        }
        if (depth == 0U && statement[offset] == ',') {
            items.push_back(
                base::trim_ascii_space(statement.substr(item_begin, offset - item_begin))
            );
            item_begin = offset + 1U;
            ++offset;
            continue;
        }
        if (depth == 0U && keyword_at(statement, offset, "from")) {
            items.push_back(
                base::trim_ascii_space(statement.substr(item_begin, offset - item_begin))
            );
            return items;
        }
        ++offset;
    }
    if (depth != 0U || state == CompatibilityScanState::kSingleQuoted
        || state == CompatibilityScanState::kDoubleQuoted
        || state == CompatibilityScanState::kBlockComment) {
        return std::nullopt;
    }
    items.push_back(base::trim_ascii_space(statement.substr(item_begin)));
    return items;
}

auto is_sum_item(const std::string_view item) noexcept -> bool {
    constexpr std::string_view k_sum = "sum";
    const std::string_view trimmed = base::trim_ascii_space(item);
    if (!ascii_starts_with(trimmed, k_sum)) {
        return false;
    }
    std::size_t offset{k_sum.size()};
    while (offset < trimmed.size() && base::is_ascii_space(trimmed[offset])) {
        ++offset;
    }
    return offset < trimmed.size() && trimmed[offset] == '(';
}

auto has_sql_after_statement_terminator(const std::string_view sql, std::size_t offset) noexcept
    -> bool {
    CompatibilityScanState state{CompatibilityScanState::kNormal};
    while (offset < sql.size()) {
        if (state != CompatibilityScanState::kNormal) {
            advance_compatibility_scan(sql, offset, state);
            continue;
        }
        if (begin_compatibility_non_normal(sql, offset, state)) {
            continue;
        }
        if (!base::is_ascii_space(sql[offset]) && sql[offset] != ';') {
            return true;
        }
        ++offset;
    }
    return false;
}

}  // namespace

auto parse_stream_control(const std::string_view sql) -> std::optional<StreamControl> {
    const std::string normalized = normalized_words(sql);
    if (normalized == "begin") {
        return StreamControl::kBegin;
    }
    if (normalized == "commit") {
        return StreamControl::kCommit;
    }
    if (normalized == "abort" || normalized == "rollback") {
        return StreamControl::kRollback;
    }
    if (normalized == "set transaction isolation level read committed") {
        return StreamControl::kReadCommitted;
    }
    if (normalized == "set transaction isolation level repeatable read"
        || normalized == "set transaction isolation level snapshot isolation") {
        return StreamControl::kRepeatableRead;
    }
    if (normalized == "set transaction isolation level serializable") {
        return StreamControl::kSerializable;
    }
    return std::nullopt;
}

auto is_load_statement(const std::string_view sql) noexcept -> bool {
    return base::ascii_equal_ignoring_case(first_keyword(sql), "load");
}

auto parse_load_statement(const std::string_view sql) -> base::Result<LoadRequest> {
    const std::string_view statement = trim_statement_terminator(sql);
    std::array<std::string_view, 4U> parts{};
    std::size_t count = 0U;
    std::size_t offset = 0U;
    while (offset < statement.size()) {
        while (offset < statement.size() && base::is_ascii_space(statement[offset])) {
            ++offset;
        }
        if (offset == statement.size()) {
            break;
        }
        const auto begin = offset;
        while (offset < statement.size() && !base::is_ascii_space(statement[offset])) {
            ++offset;
        }
        if (count == std::size(parts)) {
            return base::fail(compatibility_error(
                base::ErrorCode::kInvalidArgument,
                "LOAD syntax must be: LOAD <path> INTO <table>",
                "parse LOAD"
            ));
        }
        parts.at(count) = statement.substr(begin, offset - begin);
        ++count;
    }
    if (count != std::size(parts) || !base::ascii_equal_ignoring_case(parts[0U], "load")
        || !base::ascii_equal_ignoring_case(parts[2U], "into")) {
        return base::fail(compatibility_error(
            base::ErrorCode::kInvalidArgument,
            "LOAD syntax must be: LOAD <path> INTO <table>",
            "parse LOAD"
        ));
    }
    if (!is_simple_identifier(parts[3U])) {
        return base::fail(compatibility_error(
            base::ErrorCode::kInvalidArgument,
            "LOAD table name must be an unquoted SQL identifier",
            "parse LOAD"
        ));
    }
    return LoadRequest{
        .source = std::filesystem::path{std::string{parts[1U]}},
        .table = std::string{parts[3U]},
    };
}

auto rewrite_legacy_create_index(const std::string_view sql, std::uint64_t& next_index_id)
    -> base::Result<std::optional<std::string>> {
    constexpr std::string_view k_prefix = "create index";
    const std::string_view statement = trim_statement_terminator(sql);
    if (!ascii_starts_with(statement, k_prefix)
        || (statement.size() > k_prefix.size()
            && !base::is_ascii_space(statement[k_prefix.size()]))) {
        return std::optional<std::string>{};
    }

    const std::string_view remainder = base::trim_ascii_space(statement.substr(k_prefix.size()));
    if (remainder.empty() || has_word_on_clause(remainder)) {
        return std::optional<std::string>{};
    }
    const std::size_t open = remainder.find('(');
    const std::size_t close = remainder.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open
        || !base::trim_ascii_space(remainder.substr(close + 1U)).empty()) {
        return base::fail(compatibility_error(
            base::ErrorCode::kInvalidArgument,
            "legacy CREATE INDEX must be CREATE INDEX <table>(<column-list>)",
            "rewrite legacy index"
        ));
    }
    const std::string_view table = base::trim_ascii_space(remainder.substr(0U, open));
    const std::string_view columns =
        base::trim_ascii_space(remainder.substr(open + 1U, close - open - 1U));
    if (!is_simple_identifier(table) || columns.empty()) {
        return base::fail(compatibility_error(
            base::ErrorCode::kInvalidArgument,
            "legacy CREATE INDEX requires an unquoted table name and a nonempty column list",
            "rewrite legacy index"
        ));
    }
    if (next_index_id == std::numeric_limits<std::uint64_t>::max()) {
        return base::fail(compatibility_error(
            base::ErrorCode::kResourceExhausted,
            "legacy CREATE INDEX name sequence is exhausted",
            "rewrite legacy index"
        ));
    }
    const std::string name = "__rmdb_idx_" + std::to_string(next_index_id++);
    return std::optional<std::string>{
        "CREATE UNIQUE INDEX " + name + " ON " + std::string{table} + " (" + std::string{columns}
        + ")"
    };
}

auto has_multiple_top_level_statements(const std::string_view sql) noexcept -> bool {
    CompatibilityScanState state{CompatibilityScanState::kNormal};
    std::size_t depth{0U};
    std::size_t offset{0U};
    while (offset < sql.size()) {
        if (state != CompatibilityScanState::kNormal) {
            advance_compatibility_scan(sql, offset, state);
            continue;
        }
        if (begin_compatibility_non_normal(sql, offset, state)) {
            continue;
        }
        if (sql[offset] == '(') {
            ++depth;
        } else if (sql[offset] == ')' && depth > 0U) {
            --depth;
        } else if (sql[offset] == ';' && depth == 0U) {
            return has_sql_after_statement_terminator(sql, offset + 1U);
        }
        ++offset;
    }
    return false;
}

void normalize_empty_stream_sums(
    const std::string_view sql,
    const std::span<const protocol::ColumnMetadata> columns,
    std::vector<std::vector<types::Value>>& rows
) {
    if (rows.size() != 1U || contains_top_level_group_by(sql)) {
        return;
    }
    const auto items = top_level_select_items(sql);
    if (!items.has_value() || items->size() != columns.size()
        || rows.front().size() != columns.size()) {
        return;
    }
    for (std::size_t index = 0U; index < columns.size(); ++index) {
        if (!is_sum_item(items->at(index)) || !rows.front()[index].is_null()) {
            continue;
        }
        if (columns[index].data_type == protocol::WireDataType::kInt32) {
            rows.front()[index] = types::Value::integer(0);
        } else if (columns[index].data_type == protocol::WireDataType::kFloat32) {
            rows.front()[index] = types::Value::floating(0.0F);
        }
    }
}

}  // namespace db::server::internal
