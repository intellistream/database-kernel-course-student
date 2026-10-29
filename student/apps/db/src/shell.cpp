#include "db/shell.hpp"

#include <cctype>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/sql/parser.hpp"
#include "db/sql/source_ast.hpp"

namespace db::shell {
namespace {

inline constexpr base::ExceptionBoundaryPolicy kShellExceptionBoundary{
    .component = "shell",
    .allocation_message = "shell exhausted available memory",
    .exception_prefix = "unexpected exception escaped the shell application: ",
    .unknown_exception_message = "unknown exception escaped the shell application",
};

template <typename Callback>
auto shell_boundary(const std::string_view operation, Callback&& callback) {
    return base::invoke_exception_boundary(
        kShellExceptionBoundary,
        operation,
        std::forward<Callback>(callback)
    );
}

enum class LexicalState : std::uint8_t {
    kNormal,
    kSingleQuote,
    kDoubleQuote,
    kLineComment,
    kBlockComment,
    kDollarQuote,
};

auto ascii_space(const char character) noexcept -> bool {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r'
           || character == '\f' || character == '\v';
}

auto dollar_tag_character(const char character) noexcept -> bool {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

auto skip_dollar_tag(const std::string_view sql, const std::size_t begin) noexcept -> std::size_t {
    if (begin >= sql.size() || sql.at(begin) != '$') {
        return begin;
    }
    std::size_t position = begin + 1U;
    while (position < sql.size() && dollar_tag_character(sql.at(position))) {
        ++position;
    }
    if (position < sql.size() && sql.at(position) == '$') {
        return position + 1U;
    }
    return begin;
}

auto trim_view(const std::string_view value) noexcept -> std::string_view {
    std::size_t begin = 0U;
    while (begin < value.size() && ascii_space(value.at(begin))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && ascii_space(value.at(end - 1U))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

auto shell_error(std::string message, std::string operation) -> base::Error {
    return base::Error{base::ErrorCode::kInternal, std::move(message)}.with_context(
        "shell",
        std::move(operation)
    );
}

auto shell_argument_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "shell",
        "parse arguments"
    );
}

auto shell_unavailable_error(std::string message, std::string operation) -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, std::move(message)}.with_context(
        "shell",
        std::move(operation)
    );
}

auto to_session_isolation(const sql::IsolationLevel isolation) -> api::SessionIsolation {
    switch (isolation) {
        case sql::IsolationLevel::kReadCommitted:
            return api::SessionIsolation::kReadCommitted;
        case sql::IsolationLevel::kRepeatableRead:
            return api::SessionIsolation::kRepeatableRead;
        case sql::IsolationLevel::kSerializable:
            return api::SessionIsolation::kSerializable;
    }
    return api::SessionIsolation::kSerializable;
}

auto is_transaction_statement(const sql::SourceStatement& statement) noexcept -> bool {
    return std::holds_alternative<sql::SourceTransactionStatement>(statement.node)
           || std::holds_alternative<sql::SourceSetTransactionStatement>(statement.node);
}

auto statement_text(const std::string_view script, const sql::SourceStatement& statement)
    -> std::string {
    if (statement.span.begin_byte > script.size()
        || statement.span.end_byte < statement.span.begin_byte
        || statement.span.end_byte > script.size()) {
        return {};
    }
    return std::string{script.substr(
        statement.span.begin_byte,
        statement.span.end_byte - statement.span.begin_byte
    )};
}

class StatementScanner final {
public:
    explicit StatementScanner(const std::string_view sql) noexcept
        : sql_{sql} {}

    auto complete() noexcept -> bool {
        while (position_ < sql_.size()) {
            switch (state_) {
                case LexicalState::kNormal:
                    scan_normal();
                    break;
                case LexicalState::kSingleQuote:
                    scan_quoted('\'', true);
                    break;
                case LexicalState::kDoubleQuote:
                    scan_quoted('"', false);
                    break;
                case LexicalState::kLineComment:
                    scan_line_comment();
                    break;
                case LexicalState::kBlockComment:
                    scan_block_comment();
                    break;
                case LexicalState::kDollarQuote:
                    scan_dollar_quote();
                    break;
            }
        }
        return state_ == LexicalState::kNormal && last_significant_ != std::string_view::npos
               && sql_.at(last_significant_) == ';';
    }

private:
    auto starts_with(const std::string_view token) const noexcept -> bool {
        return sql_.substr(position_, token.size()) == token;
    }

    void scan_normal() noexcept {
        const char character = sql_.at(position_);
        if (starts_with("--")) {
            state_ = LexicalState::kLineComment;
            position_ += 2U;
            return;
        }
        if (starts_with("/*")) {
            state_ = LexicalState::kBlockComment;
            block_comment_depth_ = 1U;
            position_ += 2U;
            return;
        }
        if (character == '\'') {
            state_ = LexicalState::kSingleQuote;
            last_significant_ = position_++;
            return;
        }
        if (character == '"') {
            state_ = LexicalState::kDoubleQuote;
            last_significant_ = position_++;
            return;
        }
        if (character == '$') {
            const auto delimiter_end = skip_dollar_tag(sql_, position_);
            if (delimiter_end != position_) {
                dollar_delimiter_ = std::string{sql_.substr(position_, delimiter_end - position_)};
                state_ = LexicalState::kDollarQuote;
                last_significant_ = position_;
                position_ = delimiter_end;
                return;
            }
        }
        if (!ascii_space(character)) {
            last_significant_ = position_;
        }
        ++position_;
    }

    void scan_quoted(const char quote, const bool backslash_escape) noexcept {
        const char character = sql_.at(position_);
        if (backslash_escape && character == '\\' && position_ + 1U < sql_.size()) {
            position_ += 2U;
            return;
        }
        if (character == quote && position_ + 1U < sql_.size()
            && sql_.at(position_ + 1U) == quote) {
            position_ += 2U;
            return;
        }
        if (character == quote) {
            state_ = LexicalState::kNormal;
        }
        ++position_;
    }

    void scan_line_comment() noexcept {
        if (sql_.at(position_) == '\n' || sql_.at(position_) == '\r') {
            state_ = LexicalState::kNormal;
        }
        ++position_;
    }

    void scan_block_comment() noexcept {
        if (starts_with("/*")) {
            ++block_comment_depth_;
            position_ += 2U;
            return;
        }
        if (starts_with("*/")) {
            --block_comment_depth_;
            position_ += 2U;
            if (block_comment_depth_ == 0U) {
                state_ = LexicalState::kNormal;
            }
            return;
        }
        ++position_;
    }

    void scan_dollar_quote() noexcept {
        if (starts_with(dollar_delimiter_)) {
            position_ += dollar_delimiter_.size();
            dollar_delimiter_.clear();
            state_ = LexicalState::kNormal;
            return;
        }
        ++position_;
    }

    std::string_view sql_;
    LexicalState state_{LexicalState::kNormal};
    std::size_t position_{0U};
    std::size_t block_comment_depth_{0U};
    std::string dollar_delimiter_{};
    std::size_t last_significant_{std::string_view::npos};
};

}  // namespace

auto statement_complete(const std::string_view sql) noexcept -> bool {
    return StatementScanner{sql}.complete();
}

auto parse_shell_options(const std::span<char*> arguments) -> base::Result<ShellOptions> {
    return shell_boundary("parse arguments", [&]() -> base::Result<ShellOptions> {
        if (arguments.empty() || arguments.front() == nullptr) {
            return base::fail(
                shell_argument_error("shell arguments did not include an executable name")
            );
        }
        if (arguments.size() > 2U) {
            return base::fail(
                shell_argument_error("db accepts at most one data-directory argument")
            );
        }
        ShellOptions options;
        if (arguments.size() == 2U) {
            if (arguments[1] == nullptr || std::string_view{arguments[1]}.empty()) {
                return base::fail(
                    shell_argument_error("shell data-directory argument must not be empty")
                );
            }
            options.data_directory = arguments[1];
        }
        return options;
    });
}

auto is_quit_command(const std::string_view line) noexcept -> bool {
    return trim_view(line) == "\\q";
}

auto format_cell(const api::Value& value) -> std::string {
    if (const auto* const string = value.as_string(); string != nullptr) {
        return *string;
    }
    return value.display();
}

void print_result_set(const api::ResultSet& result_set, std::ostream& output) {
    if (result_set.empty()) {
        output << "OK\n";
        return;
    }
    const auto columns = result_set.schema().columns();
    for (std::size_t index = 0U; index < columns.size(); ++index) {
        if (index != 0U) {
            output << " | ";
        }
        output << columns[index].name;
    }
    output << '\n';
    for (const auto& row : result_set.rows()) {
        const auto values = row.values();
        for (std::size_t index = 0U; index < values.size(); ++index) {
            if (index != 0U) {
                output << " | ";
            }
            output << format_cell(values[index]);
        }
        output << '\n';
    }
    output << '(' << result_set.size() << " rows)\n";
}

void print_dispatch_result(const api::DispatchResult& result, std::ostream& output) {
    if (result.result_sets.empty()) {
        output << "OK\n";
        return;
    }
    for (const auto& result_set : result.result_sets) {
        print_result_set(result_set, output);
    }
}

struct Shell::Impl final {
    Impl(api::Database database_value, api::Session session_value) noexcept
        : database{std::move(database_value)},
          session{std::move(session_value)} {}

    Impl(const Impl&) = delete;
    auto operator=(const Impl&) -> Impl& = delete;
    Impl(Impl&&) = delete;
    auto operator=(Impl&&) -> Impl& = delete;

    ~Impl() {
        static_cast<void>(shutdown());
    }

    auto run(std::istream& input, std::ostream& output, std::ostream& errors) -> base::Status;
    auto shutdown() noexcept -> base::Status;
    void execute_script(std::string_view script, std::ostream& output, std::ostream& errors);
    void execute_source_statement(
        std::string_view script,
        const sql::SourceStatement& statement,
        std::ostream& output,
        std::ostream& errors
    );

    api::Database database;
    api::Session session;
    bool shutdown_complete{false};
};

auto Shell::open(const ShellOptions& options) -> base::Result<Shell> {
    return shell_boundary("open shell", [&]() -> base::Result<Shell> {
        api::DatabaseConfig config{
            .data_directory = options.data_directory,
            .buffer_pool_pages = options.buffer_pool_pages,
            .wal_ring_pages = options.wal_ring_pages,
        };
        auto database = api::Database::open_or_create(config);
        if (!database) {
            return base::fail(std::move(database).error().with_context("shell", "open database"));
        }
        auto session = database->new_session();
        if (!session) {
            return base::fail(std::move(session).error().with_context("shell", "create session"));
        }
        return Shell{
            std::make_unique<Impl>(std::move(database).value(), std::move(session).value())
        };
    });
}

Shell::Shell(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

Shell::Shell(Shell&& other) noexcept = default;

Shell::~Shell() = default;

auto Shell::run(std::istream& input, std::ostream& output, std::ostream& errors) -> base::Status {
    return shell_boundary("run shell", [&]() -> base::Status {
        if (implementation_ == nullptr) {
            return base::fail(shell_unavailable_error("shell has been moved from", "run"));
        }
        return implementation_->run(input, output, errors);
    });
}

auto Shell::Impl::run(std::istream& input, std::ostream& output, std::ostream& errors)
    -> base::Status {
    if (shutdown_complete) {
        return base::fail(shell_unavailable_error("shell has already been shut down", "run"));
    }

    output << "DB SQL shell. End statements with ';', use \\q to quit.\n";
    std::string pending;
    for (;;) {
        output << (pending.empty() ? "db> " : "     -> ");
        output.flush();
        if (!input.good()) {
            break;
        }
        std::string line;
        if (!std::getline(input, line)) {
            if (input.bad()) {
                return base::fail(shell_error("could not read shell input", "read input"));
            }
            if (!pending.empty()) {
                errors << "ERROR: incomplete SQL statement at end of input\n";
                pending.clear();
            }
            break;
        }
        if (pending.empty() && is_quit_command(line)) {
            break;
        }
        pending.append(line);
        pending.push_back('\n');
        if (!statement_complete(pending)) {
            continue;
        }
        execute_script(pending, output, errors);
        pending.clear();
    }
    return shutdown();
}

auto Shell::Impl::shutdown() noexcept -> base::Status {
    if (shutdown_complete) {
        return {};
    }
    if (session.state() != api::SessionState::kIdle) {
        if (auto status = session.rollback(); !status) {
            return status;
        }
    }
    if (auto status = session.close(); !status) {
        return status;
    }
    auto status = database.close();
    if (status) {
        shutdown_complete = true;
    }
    return status;
}

void Shell::Impl::execute_script(
    const std::string_view script,
    std::ostream& output,
    std::ostream& errors
) {
    auto parsed = sql::parse_sql(script);
    if (!parsed) {
        errors << "ERROR: " << parsed.error().to_string() << '\n';
        return;
    }
    for (const auto& statement : parsed->statements) {
        execute_source_statement(script, statement, output, errors);
    }
}

void Shell::Impl::execute_source_statement(
    const std::string_view script,
    const sql::SourceStatement& statement,
    std::ostream& output,
    std::ostream& errors
) {
    if (is_transaction_statement(statement)) {
        base::Status status;
        if (const auto* const transaction =
                std::get_if<sql::SourceTransactionStatement>(&statement.node);
            transaction != nullptr) {
            switch (transaction->kind) {
                case sql::TransactionKind::kBegin:
                    status = session.begin();
                    break;
                case sql::TransactionKind::kCommit:
                    status = session.commit();
                    break;
                case sql::TransactionKind::kRollback:
                    status = session.rollback();
                    break;
            }
        } else {
            const auto* const set_transaction =
                std::get_if<sql::SourceSetTransactionStatement>(&statement.node);
            if (set_transaction == nullptr) {
                errors << "ERROR: "
                       << shell_error(
                              "transaction statement has an unrecognized source node",
                              "execute transaction statement"
                          )
                              .to_string()
                       << '\n';
                return;
            }
            status = session.set_isolation(to_session_isolation(set_transaction->isolation));
        }
        if (!status) {
            errors << "ERROR: " << status.error().to_string() << '\n';
        } else {
            output << "OK\n";
        }
        return;
    }

    const auto statement_sql = statement_text(script, statement);
    if (statement_sql.empty()) {
        errors << "ERROR: "
               << shell_error("invalid statement source span", "slice statement").to_string()
               << '\n';
        return;
    }
    auto dispatched = database.dispatch(session, statement_sql);
    if (!dispatched) {
        errors << "ERROR: " << dispatched.error().to_string() << '\n';
        return;
    }
    print_dispatch_result(dispatched.value(), output);
}

}  // namespace db::shell
