#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/engine/database.hpp"
#include "db/engine/session.hpp"
#include "db/test_support/test_directory.hpp"

#include "sql_regress_internal.hpp"

namespace db::test_support::detail {
namespace {

struct ScriptChunk final {
    std::string source;
};

struct CopyFixture final {
    std::string table;
    std::string path;
};

enum class StatementClass : std::uint8_t {
    kOther,
    kQuery,
    kBegin,
    kCommit,
    kRollback,
    kCopy,
};

auto strip_leading_trivia(std::string_view input) noexcept -> std::string_view {
    for (;;) {
        input = trim_view(input);
        if (input.starts_with("--")) {
            input.remove_prefix(2U);
            const auto newline = input.find('\n');
            input =
                newline == std::string_view::npos ? std::string_view{} : input.substr(newline + 1U);
            continue;
        }
        if (input.starts_with("/*")) {
            const auto end = input.find("*/", 2U);
            if (end != std::string_view::npos) {
                input.remove_prefix(end + 2U);
                continue;
            }
        }
        return input;
    }
}

auto dollar_delimiter(const std::string_view input) -> std::optional<std::string> {
    if (input.empty() || input.front() != '$') {
        return std::nullopt;
    }
    const auto end = input.find('$', 1U);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    const auto tag = input.substr(1U, end - 1U);
    if (!std::all_of(tag.begin(), tag.end(), [](const char value) {
            const auto byte = static_cast<unsigned char>(value);
            return std::isalnum(byte) != 0 || value == '_';
        })) {
        return std::nullopt;
    }
    return std::string{input.substr(0U, end + 1U)};
}

enum class ScriptLexState : std::uint8_t {
    kNormal,
    kSingleQuote,
    kDoubleQuote,
    kLineComment,
    kBlockComment,
    kDollarQuote,
};

class ScriptScanner final {
public:
    explicit ScriptScanner(const std::string_view bytes) noexcept
        : bytes_(bytes) {}

    auto at_end() const noexcept -> bool {
        return index_ >= bytes_.size();
    }

    auto position() const noexcept -> std::size_t {
        return index_;
    }

    auto consume() -> bool {
        switch (state_) {
            case ScriptLexState::kNormal:
                return consume_normal();
            case ScriptLexState::kSingleQuote:
                consume_quoted('\'');
                return false;
            case ScriptLexState::kDoubleQuote:
                consume_quoted('"');
                return false;
            case ScriptLexState::kLineComment:
                consume_line_comment();
                return false;
            case ScriptLexState::kBlockComment:
                consume_block_comment();
                return false;
            case ScriptLexState::kDollarQuote:
                consume_dollar_quote();
                return false;
        }
        return false;
    }

private:
    auto current() const noexcept -> char {
        return bytes_[index_];
    }

    auto starts_with(const std::string_view token) const noexcept -> bool {
        return bytes_.substr(index_, token.size()) == token;
    }

    void advance(const std::size_t count = 1U) noexcept {
        index_ += count;
    }

    auto consume_normal() -> bool {
        const auto character = current();
        if (character == '\'') {
            state_ = ScriptLexState::kSingleQuote;
            advance();
            return false;
        }
        if (character == '"') {
            state_ = ScriptLexState::kDoubleQuote;
            advance();
            return false;
        }
        if (character == '-' && starts_with("--")) {
            state_ = ScriptLexState::kLineComment;
            advance(2U);
            return false;
        }
        if (character == '/' && starts_with("/*")) {
            state_ = ScriptLexState::kBlockComment;
            block_depth_ = 1U;
            advance(2U);
            return false;
        }
        if (character == '$' && begin_dollar_quote()) {
            return false;
        }
        if (character == ';') {
            consume_statement_delimiter();
            return true;
        }
        advance();
        return false;
    }

    void consume_quoted(const char quote) noexcept {
        if (current() != quote) {
            advance();
            return;
        }
        if (index_ + 1U < bytes_.size() && bytes_[index_ + 1U] == quote) {
            advance(2U);
            return;
        }
        state_ = ScriptLexState::kNormal;
        advance();
    }

    void consume_line_comment() noexcept {
        const auto character = current();
        advance();
        if (character == '\n') {
            state_ = ScriptLexState::kNormal;
        }
    }

    void consume_block_comment() noexcept {
        if (starts_with("/*")) {
            ++block_depth_;
            advance(2U);
            return;
        }
        if (!starts_with("*/")) {
            advance();
            return;
        }
        --block_depth_;
        advance(2U);
        if (block_depth_ == 0U) {
            state_ = ScriptLexState::kNormal;
        }
    }

    void consume_dollar_quote() noexcept {
        if (!starts_with(dollar_tag_)) {
            advance();
            return;
        }
        advance(dollar_tag_.size());
        dollar_tag_.clear();
        state_ = ScriptLexState::kNormal;
    }

    auto begin_dollar_quote() -> bool {
        auto delimiter = dollar_delimiter(bytes_.substr(index_));
        if (!delimiter.has_value()) {
            return false;
        }
        dollar_tag_ = std::move(delimiter).value();
        advance(dollar_tag_.size());
        state_ = ScriptLexState::kDollarQuote;
        return true;
    }

    void consume_statement_delimiter() noexcept {
        advance();
        if (!at_end() && current() == '\n') {
            advance();
        }
    }

    std::string_view bytes_;
    std::size_t index_ = 0U;
    std::size_t block_depth_ = 0U;
    std::string dollar_tag_;
    ScriptLexState state_ = ScriptLexState::kNormal;
};

auto split_script(const std::string_view script) -> std::vector<ScriptChunk> {
    std::vector<ScriptChunk> chunks;
    std::size_t start = 0U;
    ScriptScanner scanner{script};

    while (!scanner.at_end()) {
        if (!scanner.consume()) {
            continue;
        }
        const auto end = scanner.position();
        chunks.push_back(ScriptChunk{std::string{script.substr(start, end - start)}});
        start = end;
    }

    if (start < script.size()) {
        chunks.push_back(ScriptChunk{std::string{script.substr(start)}});
    }
    return chunks;
}

auto starts_with_keyword(const std::string_view input, const std::string_view keyword) noexcept
    -> bool {
    if (input.size() < keyword.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < keyword.size(); ++index) {
        const auto left = static_cast<unsigned char>(input[index]);
        const auto right = static_cast<unsigned char>(keyword[index]);
        if (std::toupper(left) != std::toupper(right)) {
            return false;
        }
    }
    return input.size() == keyword.size() || is_ascii_space(input[keyword.size()])
           || input[keyword.size()] == ';';
}

auto classify_statement(const std::string_view sql) noexcept -> StatementClass {
    if (starts_with_keyword(sql, "BEGIN")) {
        return StatementClass::kBegin;
    }
    if (starts_with_keyword(sql, "COMMIT")) {
        return StatementClass::kCommit;
    }
    if (starts_with_keyword(sql, "ROLLBACK")) {
        return StatementClass::kRollback;
    }
    if (starts_with_keyword(sql, "COPY")) {
        return StatementClass::kCopy;
    }
    if (starts_with_keyword(sql, "SELECT") || starts_with_keyword(sql, "SHOW")
        || starts_with_keyword(sql, "EXPLAIN")) {
        return StatementClass::kQuery;
    }
    return StatementClass::kOther;
}

auto find_ascii_case_insensitive(
    const std::string_view input,
    const std::string_view needle
) noexcept -> std::size_t {
    if (needle.empty() || input.size() < needle.size()) {
        return std::string_view::npos;
    }
    for (std::size_t start = 0U; start + needle.size() <= input.size(); ++start) {
        bool matches = true;
        for (std::size_t offset = 0U; offset < needle.size(); ++offset) {
            const auto left = static_cast<unsigned char>(input[start + offset]);
            const auto right = static_cast<unsigned char>(needle[offset]);
            if (std::toupper(left) != std::toupper(right)) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return start;
        }
    }
    return std::string_view::npos;
}

auto parse_copy_command(const std::string_view sql) -> base::Result<std::optional<CopyFixture>> {
    if (!starts_with_keyword(sql, "COPY")) {
        return std::optional<CopyFixture>{};
    }
    auto statement = trim_view(sql);
    if (!statement.empty() && statement.back() == ';') {
        statement.remove_suffix(1U);
        statement = trim_view(statement);
    }
    const auto separator = find_ascii_case_insensitive(statement, " FROM ");
    if (separator == std::string_view::npos) {
        return base::fail(regression_error(
            base::ErrorCode::kInvalidArgument,
            "expected COPY table FROM 'relative/path'",
            "parse COPY fixture"
        ));
    }
    const auto table = trim_view(statement.substr(4U, separator - 4U));
    const auto path_literal = trim_view(statement.substr(separator + 6U));
    if (table.empty() || !std::all_of(table.begin(), table.end(), [](const char value) {
            const auto byte = static_cast<unsigned char>(value);
            return std::isalnum(byte) != 0 || value == '_';
        })) {
        return base::fail(regression_error(
            base::ErrorCode::kInvalidArgument,
            "invalid COPY table name " + std::string{table},
            "parse COPY fixture"
        ));
    }
    if (path_literal.size() < 2U || path_literal.front() != '\'' || path_literal.back() != '\'') {
        return base::fail(regression_error(
            base::ErrorCode::kInvalidArgument,
            "COPY fixture path must be single-quoted",
            "parse COPY fixture"
        ));
    }
    const auto path = path_literal.substr(1U, path_literal.size() - 2U);
    if (path.empty()) {
        return base::fail(regression_error(
            base::ErrorCode::kInvalidArgument,
            "COPY fixture path must not be empty",
            "parse COPY fixture"
        ));
    }
    return std::optional<CopyFixture>{CopyFixture{
        .table = std::string{table},
        .path = std::string{path},
    }};
}

auto is_transaction_terminator(const StatementClass kind) noexcept -> bool {
    return kind == StatementClass::kCommit || kind == StatementClass::kRollback;
}

auto is_transaction_command(const StatementClass kind) noexcept -> bool {
    return kind == StatementClass::kBegin || is_transaction_terminator(kind);
}

auto is_safe_fixture_path(const std::filesystem::path& path) -> bool {
    if (path.is_absolute()) {
        return false;
    }
    return std::all_of(path.begin(), path.end(), [](const std::filesystem::path& component) {
        return component != "." && component != "..";
    });
}

class FixtureTransactionState final {
public:
    auto rejects(const StatementClass kind) const noexcept -> bool {
        return explicit_transaction_ && aborted_ && !is_transaction_terminator(kind);
    }

    auto needs_abort_rollback(const StatementClass kind) const noexcept -> bool {
        return explicit_transaction_ && aborted_ && is_transaction_terminator(kind);
    }

    auto is_explicit() const noexcept -> bool {
        return explicit_transaction_;
    }

    void begin() noexcept {
        explicit_transaction_ = true;
        aborted_ = false;
    }

    void finish() noexcept {
        explicit_transaction_ = false;
        aborted_ = false;
    }

    void mark_statement_failure() noexcept {
        if (explicit_transaction_) {
            aborted_ = true;
        }
    }

    void mark_transaction_failure(const StatementClass kind) noexcept {
        if (explicit_transaction_ && kind != StatementClass::kRollback) {
            aborted_ = true;
        }
    }

private:
    bool explicit_transaction_ = false;
    bool aborted_ = false;
};

class FixtureScriptExecutor final {
public:
    FixtureScriptExecutor(
        engine::Database& database,
        engine::Session& session,
        std::filesystem::path fixture_root
    )
        : database_(database),
          session_(session),
          fixture_root_(std::move(fixture_root)) {}

    auto run(const std::string_view source) -> std::string {
        for (const auto& chunk : split_script(source)) {
            execute_chunk(chunk);
        }
        rollback_open_transaction();
        return std::move(output_);
    }

private:
    void execute_chunk(const ScriptChunk& chunk) {
        output_.append(chunk.source);
        const auto sql = trim_view(strip_leading_trivia(chunk.source));
        if (sql.empty()) {
            return;
        }
        const auto kind = classify_statement(sql);
        if (transaction_.rejects(kind)) {
            output_.append(format_transaction_aborted());
            return;
        }
        if (kind == StatementClass::kCopy) {
            execute_copy(sql);
            return;
        }
        if (transaction_.needs_abort_rollback(kind)) {
            execute_abort_rollback();
            return;
        }
        if (is_transaction_command(kind)) {
            execute_transaction_command(kind);
            return;
        }
        execute_dispatched_statement(kind, sql);
    }

    void execute_copy(const std::string_view sql) {
        auto copy = parse_copy_command(sql);
        if (!copy) {
            append_statement_error(copy.error());
            return;
        }
        if (!copy->has_value()) {
            output_.append(format_error(regression_error(
                base::ErrorCode::kInvalidArgument,
                "COPY statement is not a fixture command",
                "execute COPY fixture"
            )));
            return;
        }
        const auto relative = std::filesystem::path{copy->value().path};
        if (!is_safe_fixture_path(relative)) {
            append_statement_error(regression_error(
                base::ErrorCode::kInvalidArgument,
                "COPY fixture path escapes fixture root",
                "execute COPY fixture"
            ));
            return;
        }
        const auto fixture_path = fixture_root_ / relative;
        std::ifstream input(fixture_path, std::ios::in | std::ios::binary);
        if (!input) {
            append_statement_error(regression_error(
                base::ErrorCode::kIo,
                "could not read COPY fixture " + fixture_path.string(),
                "execute COPY fixture"
            ));
            return;
        }
        const auto copy_sql = "COPY " + copy->value().table + " FROM STDIN;";
        auto copied = database_.copy_from(copy_sql, input, session_);
        if (!copied) {
            append_statement_error(copied.error());
        }
    }

    void execute_abort_rollback() {
        auto rolled_back = session_.rollback();
        if (!rolled_back) {
            output_.append(format_error(rolled_back.error()));
            return;
        }
        transaction_.finish();
    }

    void execute_transaction_command(const StatementClass kind) {
        auto status = transaction_command(kind);
        if (!status) {
            output_.append(format_error(status.error()));
            transaction_.mark_transaction_failure(kind);
            return;
        }
        if (kind == StatementClass::kBegin) {
            transaction_.begin();
            return;
        }
        transaction_.finish();
    }

    auto transaction_command(const StatementClass kind) -> base::Status {
        if (kind == StatementClass::kBegin) {
            return session_.begin();
        }
        if (kind == StatementClass::kCommit) {
            return session_.commit();
        }
        return session_.rollback();
    }

    void execute_dispatched_statement(const StatementClass kind, const std::string_view sql) {
        auto dispatched = database_.dispatch(session_, sql);
        if (!dispatched) {
            append_statement_error(dispatched.error());
            return;
        }
        if (kind != StatementClass::kQuery) {
            return;
        }
        for (const auto& result_set : dispatched->result_sets) {
            output_.append(format_table(result_set));
        }
    }

    void append_statement_error(const base::Error& error) {
        output_.append(format_error(error));
        transaction_.mark_statement_failure();
    }

    void rollback_open_transaction() {
        if (!transaction_.is_explicit() || session_.state() == engine::SessionState::kIdle) {
            return;
        }
        static_cast<void>(session_.rollback());
        transaction_.finish();
    }

    engine::Database& database_;
    engine::Session& session_;
    std::filesystem::path fixture_root_;
    FixtureTransactionState transaction_;
    std::string output_;
};

}  // namespace

auto execute_fixture_script(
    const std::string_view source,
    const std::filesystem::path& fixture_root,
    const std::string_view name
) -> base::Result<std::string> {
    auto directory = TestDirectory::create(std::string{"sql-regress-"} + std::string{name});
    if (!directory) {
        return base::fail(std::move(directory).error());
    }
    engine::DatabaseConfig config{
        .data_directory = directory->path(),
        .buffer_pool_pages = 256U,
        .wal_ring_pages = 256U,
    };
    auto database = engine::Database::create(config);
    if (!database) {
        return base::fail(std::move(database).error());
    }
    auto session = database->new_session();
    if (!session) {
        return base::fail(std::move(session).error());
    }

    FixtureScriptExecutor executor{*database, *session, fixture_root};
    auto output = executor.run(normalize_output(std::string{source}));
    if (auto closed = session->close(); !closed) {
        return base::fail(std::move(closed).error());
    }
    if (auto closed = database->close(); !closed) {
        return base::fail(std::move(closed).error());
    }
    return output;
}

}  // namespace db::test_support::detail
