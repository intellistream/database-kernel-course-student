#include "db/api/api.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/engine/database.hpp"
#include "db/engine/dispatch.hpp"
#include "db/engine/result.hpp"
#include "db/engine/session.hpp"

namespace db::api {

namespace detail {

class ResultFactory final {
public:
    static auto from_engine(engine::ResultSet source) -> ResultSet {
        engine::ResultSetData data = std::move(source).into_data();
        std::vector<ResultColumn> columns;
        columns.reserve(data.columns.size());
        for (engine::ResultColumn& column : data.columns) {
            columns.push_back(
                ResultColumn{
                    .name = std::move(column.name),
                    .data_type = column.data_type,
                }
            );
        }

        auto schema = std::make_shared<const ResultSchema>(ResultSchema{std::move(columns)});
        std::vector<ResultRow> rows;
        rows.reserve(data.rows.size());
        for (auto& values : data.rows) {
            rows.push_back(ResultRow{schema, std::move(values)});
        }
        return ResultSet{std::move(schema), std::move(rows)};
    }
};

}  // namespace detail

namespace {

inline constexpr base::ExceptionBoundaryPolicy kApiExceptionBoundary{
    .component = "api",
    .allocation_message = "embedded API could not allocate bounded facade state",
    .exception_prefix = "unexpected exception escaped the embedded API: ",
    .unknown_exception_message = "unknown exception escaped the embedded API",
};

template <typename Callback>
auto api_boundary(const std::string_view operation, Callback&& callback) {
    return base::invoke_exception_boundary(
        kApiExceptionBoundary,
        operation,
        std::forward<Callback>(callback)
    );
}

auto api_error(const base::ErrorCode code, std::string message, std::string operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context("api", std::move(operation));
}

auto unavailable(std::string message, std::string operation) -> base::Error {
    return api_error(base::ErrorCode::kUnavailable, std::move(message), std::move(operation));
}

auto engine_config(const DatabaseConfig& config) -> engine::DatabaseConfig {
    return engine::DatabaseConfig{
        .data_directory = config.data_directory,
        .buffer_pool_pages = config.buffer_pool_pages,
        .recovery_page_cache_pages = config.recovery_page_cache_pages,
        .wal_ring_pages = config.wal_ring_pages,
        .timeline = config.timeline,
        .system_identifier = config.system_identifier,
        .directory_permissions = config.directory_permissions,
        .maximum_catalog_image_size = config.maximum_catalog_image_size,
        .maximum_encoded_row_size = config.maximum_encoded_row_size,
    };
}

auto database_state(const engine::DatabaseState state) noexcept -> DatabaseState {
    switch (state) {
        case engine::DatabaseState::kShutdown:
            return DatabaseState::kShutdown;
        case engine::DatabaseState::kStartingUp:
            return DatabaseState::kStartingUp;
        case engine::DatabaseState::kInProduction:
            return DatabaseState::kInProduction;
        case engine::DatabaseState::kShuttingDown:
            return DatabaseState::kShuttingDown;
    }
    return DatabaseState::kShutdown;
}

auto session_state(const engine::SessionState state) noexcept -> SessionState {
    switch (state) {
        case engine::SessionState::kIdle:
            return SessionState::kIdle;
        case engine::SessionState::kInTransaction:
            return SessionState::kInTransaction;
        case engine::SessionState::kFailedTransaction:
            return SessionState::kFailedTransaction;
    }
    return SessionState::kIdle;
}

auto session_isolation(const engine::SessionIsolation isolation) noexcept -> SessionIsolation {
    switch (isolation) {
        case engine::SessionIsolation::kReadCommitted:
            return SessionIsolation::kReadCommitted;
        case engine::SessionIsolation::kRepeatableRead:
            return SessionIsolation::kRepeatableRead;
        case engine::SessionIsolation::kSerializable:
            return SessionIsolation::kSerializable;
    }
    return SessionIsolation::kReadCommitted;
}

auto engine_isolation(const SessionIsolation isolation) noexcept -> engine::SessionIsolation {
    switch (isolation) {
        case SessionIsolation::kReadCommitted:
            return engine::SessionIsolation::kReadCommitted;
        case SessionIsolation::kRepeatableRead:
            return engine::SessionIsolation::kRepeatableRead;
        case SessionIsolation::kSerializable:
            return engine::SessionIsolation::kSerializable;
    }
    return engine::SessionIsolation::kReadCommitted;
}

auto statement_kind(const engine::DispatchStatementKind kind) noexcept -> DispatchStatementKind {
    switch (kind) {
        case engine::DispatchStatementKind::kQuery:
            return DispatchStatementKind::kQuery;
        case engine::DispatchStatementKind::kInsert:
            return DispatchStatementKind::kInsert;
        case engine::DispatchStatementKind::kUpdate:
            return DispatchStatementKind::kUpdate;
        case engine::DispatchStatementKind::kDelete:
            return DispatchStatementKind::kDelete;
        case engine::DispatchStatementKind::kTransaction:
            return DispatchStatementKind::kTransaction;
        case engine::DispatchStatementKind::kUtility:
            return DispatchStatementKind::kUtility;
        case engine::DispatchStatementKind::kExplain:
            return DispatchStatementKind::kExplain;
        case engine::DispatchStatementKind::kScript:
            return DispatchStatementKind::kScript;
    }
    return DispatchStatementKind::kScript;
}

auto dispatch_result(engine::DispatchResult source) -> DispatchResult {
    DispatchResult converted{
        .kind = statement_kind(source.kind),
        .statement_count = source.statement_count,
        .row_count = source.row_count,
        .affected_row_count = source.affected_row_count,
        .result_sets = {},
    };
    converted.result_sets.reserve(source.result_sets.size());
    for (engine::ResultSet& source_set : source.result_sets) {
        converted.result_sets.push_back(detail::ResultFactory::from_engine(std::move(source_set)));
    }
    return converted;
}

}  // namespace

struct Session::Impl final {
    explicit Impl(engine::Session session_value) noexcept
        : session{std::move(session_value)} {}

    engine::Session session;
};

struct PreparedStatement::Impl final {
    explicit Impl(engine::PreparedStatement statement_value)
        : statement{std::move(statement_value)} {
        result_columns.reserve(statement.result_columns().size());
        for (const engine::PreparedColumn& column : statement.result_columns()) {
            result_columns.push_back(
                PreparedColumn{
                    .name = column.name,
                    .data_type = column.data_type,
                }
            );
        }
    }

    engine::PreparedStatement statement;
    std::vector<PreparedColumn> result_columns;
};

struct Database::Impl final {
    Impl(engine::Database database_value, DatabaseConfig config_value) noexcept
        : database{std::move(database_value)},
          config{std::move(config_value)} {}

    engine::Database database;
    DatabaseConfig config;
};

Session::Session(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

Session::Session(Session&& other) noexcept = default;

auto Session::operator=(Session&& other) noexcept -> Session& = default;

Session::~Session() = default;

auto Session::valid() const noexcept -> bool {
    return implementation_ != nullptr && implementation_->session.valid();
}

auto Session::state() const noexcept -> SessionState {
    return !valid() ? SessionState::kIdle : session_state(implementation_->session.state());
}

auto Session::isolation() const noexcept -> SessionIsolation {
    return !valid() ? SessionIsolation::kReadCommitted
                    : session_isolation(implementation_->session.isolation());
}

auto Session::command_active() const noexcept -> bool {
    return valid() && implementation_->session.command_active();
}

auto Session::set_isolation(const SessionIsolation isolation) -> base::Status {
    return api_boundary("set session isolation", [&]() -> base::Status {
        if (!valid()) {
            return base::fail(unavailable("session has been closed", "set session isolation"));
        }
        return implementation_->session.set_isolation(engine_isolation(isolation));
    });
}

auto Session::begin() -> base::Status {
    return api_boundary("begin transaction", [&]() -> base::Status {
        if (!valid()) {
            return base::fail(unavailable("session has been closed", "begin transaction"));
        }
        return implementation_->session.begin();
    });
}

auto Session::commit() -> base::Status {
    return api_boundary("commit transaction", [&]() -> base::Status {
        if (!valid()) {
            return base::fail(unavailable("session has been closed", "commit transaction"));
        }
        return implementation_->session.commit();
    });
}

auto Session::rollback() -> base::Status {
    return api_boundary("rollback transaction", [&]() -> base::Status {
        if (!valid()) {
            return base::fail(unavailable("session has been closed", "rollback transaction"));
        }
        return implementation_->session.rollback();
    });
}

auto Session::start_command() -> base::Status {
    return api_boundary("start command", [&]() -> base::Status {
        if (!valid()) {
            return base::fail(unavailable("session has been closed", "start command"));
        }
        return implementation_->session.start_command();
    });
}

auto Session::finish_command(const bool succeeded) -> base::Status {
    return api_boundary("finish command", [&]() -> base::Status {
        if (!valid()) {
            return base::fail(unavailable("session has been closed", "finish command"));
        }
        return implementation_->session.finish_command(succeeded);
    });
}

auto Session::close() -> base::Status {
    return api_boundary("close session", [&]() -> base::Status {
        if (implementation_ == nullptr) {
            return {};
        }
        auto closed = implementation_->session.close();
        if (!closed) {
            return closed;
        }
        implementation_.reset();
        return {};
    });
}

PreparedStatement::PreparedStatement(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

PreparedStatement::PreparedStatement(PreparedStatement&& other) noexcept = default;

auto PreparedStatement::operator=(PreparedStatement&& other) noexcept
    -> PreparedStatement& = default;

PreparedStatement::~PreparedStatement() = default;

auto PreparedStatement::valid() const noexcept -> bool {
    return implementation_ != nullptr && implementation_->statement.valid();
}

auto PreparedStatement::kind() const noexcept -> DispatchStatementKind {
    return implementation_ == nullptr ? DispatchStatementKind::kScript
                                      : statement_kind(implementation_->statement.kind());
}

auto PreparedStatement::catalog_generation() const noexcept -> std::uint64_t {
    return implementation_ == nullptr ? 0U : implementation_->statement.catalog_generation();
}

auto PreparedStatement::parameter_count() const noexcept -> std::size_t {
    return implementation_ == nullptr ? 0U : implementation_->statement.parameter_count();
}

auto PreparedStatement::parameter_types() const noexcept -> std::span<const types::DataType> {
    return implementation_ == nullptr ? std::span<const types::DataType>{}
                                      : implementation_->statement.parameter_types();
}

auto PreparedStatement::result_columns() const noexcept -> std::span<const PreparedColumn> {
    return implementation_ == nullptr ? std::span<const PreparedColumn>{}
                                      : std::span<const PreparedColumn>{
                                            implementation_->result_columns,
                                        };
}

Database::Database(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

Database::Database(Database&& other) noexcept = default;

Database::~Database() = default;

auto Database::create(const DatabaseConfig& config) -> base::Result<Database> {
    return open_impl(config, true);
}

auto Database::open(const DatabaseConfig& config) -> base::Result<Database> {
    return open_impl(config, false);
}

auto Database::open_or_create(const DatabaseConfig& config) -> base::Result<Database> {
    return api_boundary("open or create database", [&]() -> base::Result<Database> {
        auto opened = engine::Database::open_or_create(engine_config(config));
        if (!opened) {
            return base::fail(std::move(opened).error());
        }
        return Database{std::make_unique<Impl>(std::move(opened).value(), config)};
    });
}

auto Database::create(const std::filesystem::path& data_directory, DatabaseConfig config)
    -> base::Result<Database> {
    return api_boundary("create database", [&]() -> base::Result<Database> {
        config.data_directory = data_directory;
        return create(config);
    });
}

auto Database::open(const std::filesystem::path& data_directory, DatabaseConfig config)
    -> base::Result<Database> {
    return api_boundary("open database", [&]() -> base::Result<Database> {
        config.data_directory = data_directory;
        return open(config);
    });
}

auto Database::open_or_create(const std::filesystem::path& data_directory, DatabaseConfig config)
    -> base::Result<Database> {
    return api_boundary("open or create database", [&]() -> base::Result<Database> {
        config.data_directory = data_directory;
        return open_or_create(config);
    });
}

auto Database::open_impl(const DatabaseConfig& config, const bool create_database)
    -> base::Result<Database> {
    const std::string_view operation = create_database ? "create database" : "open database";
    return api_boundary(operation, [&]() -> base::Result<Database> {
        auto opened = create_database ? engine::Database::create(engine_config(config))
                                      : engine::Database::open(engine_config(config));
        if (!opened) {
            return base::fail(std::move(opened).error());
        }
        return Database{std::make_unique<Impl>(std::move(opened).value(), config)};
    });
}

auto Database::close() -> base::Status {
    return api_boundary("close database", [&]() -> base::Status {
        if (implementation_ == nullptr) {
            return {};
        }
        auto closed = implementation_->database.close();
        if (!closed) {
            return closed;
        }
        implementation_.reset();
        return {};
    });
}

auto Database::is_open() const noexcept -> bool {
    return implementation_ != nullptr && implementation_->database.is_open();
}

auto Database::state() const noexcept -> DatabaseState {
    return implementation_ == nullptr ? DatabaseState::kShutdown
                                      : database_state(implementation_->database.state());
}

auto Database::data_directory() const noexcept -> const std::filesystem::path& {
    static const std::filesystem::path kEmptyPath;
    return implementation_ == nullptr ? kEmptyPath : implementation_->config.data_directory;
}

auto Database::system_identifier() const noexcept -> std::uint64_t {
    return implementation_ == nullptr ? 0U : implementation_->database.system_identifier();
}

auto Database::config() const noexcept -> const DatabaseConfig& {
    static const DatabaseConfig kEmptyConfig;
    return implementation_ == nullptr ? kEmptyConfig : implementation_->config;
}

auto Database::new_session() -> base::Result<Session> {
    return api_boundary("create session", [&]() -> base::Result<Session> {
        if (implementation_ == nullptr) {
            return base::fail(unavailable("database is not open", "create session"));
        }
        auto session = implementation_->database.new_session();
        if (!session) {
            return base::fail(std::move(session).error());
        }
        return Session{std::make_unique<Session::Impl>(std::move(session).value())};
    });
}

auto Database::session() -> base::Result<Session> {
    return new_session();
}

auto Database::static_checkpoint(Session& session, const StaticCheckpointOptions options)
    -> base::Status {
    return api_boundary("static checkpoint", [&]() -> base::Status {
        if (implementation_ == nullptr) {
            return base::fail(unavailable("database is not open", "static checkpoint"));
        }
        if (!session.valid()) {
            return base::fail(unavailable("session has been closed", "static checkpoint"));
        }
        return implementation_->database.static_checkpoint(
            session.implementation_->session,
            engine::StaticCheckpointOptions{.drain_timeout = options.drain_timeout}
        );
    });
}

auto Database::dispatch(
    Session& session,
    const std::string_view sql,
    const DispatchParameters parameters
) -> base::Result<DispatchResult> {
    return api_boundary("dispatch SQL", [&]() -> base::Result<DispatchResult> {
        if (implementation_ == nullptr) {
            return base::fail(unavailable("database is not open", "dispatch SQL"));
        }
        if (!session.valid()) {
            return base::fail(unavailable("session has been closed", "dispatch SQL"));
        }
        auto dispatched = implementation_->database.dispatch(
            session.implementation_->session,
            sql,
            engine::DispatchParameters{
                .types = parameters.types,
                .values = parameters.values,
            }
        );
        if (!dispatched) {
            return base::fail(std::move(dispatched).error());
        }
        return dispatch_result(std::move(dispatched).value());
    });
}

auto Database::prepare(
    const std::string_view sql,
    const std::span<const types::DataType> parameter_types
) -> base::Result<PreparedStatement> {
    return api_boundary("prepare SQL", [&]() -> base::Result<PreparedStatement> {
        if (implementation_ == nullptr) {
            return base::fail(unavailable("database is not open", "prepare SQL"));
        }
        auto prepared = implementation_->database.prepare(sql, parameter_types);
        if (!prepared) {
            return base::fail(std::move(prepared).error());
        }
        return PreparedStatement{
            std::make_unique<PreparedStatement::Impl>(std::move(prepared).value())
        };
    });
}

auto Database::execute_prepared(
    Session& session,
    const PreparedStatement& statement,
    const std::span<const types::Value> parameter_values
) -> base::Result<DispatchResult> {
    return api_boundary("execute prepared statement", [&]() -> base::Result<DispatchResult> {
        if (implementation_ == nullptr) {
            return base::fail(unavailable("database is not open", "execute prepared statement"));
        }
        if (!session.valid()) {
            return base::fail(unavailable("session has been closed", "execute prepared statement"));
        }
        if (!statement.valid()) {
            return base::fail(
                unavailable("prepared statement has been moved from", "execute prepared statement")
            );
        }
        auto executed = implementation_->database.execute_prepared(
            session.implementation_->session,
            statement.implementation_->statement,
            parameter_values
        );
        if (!executed) {
            return base::fail(std::move(executed).error());
        }
        return dispatch_result(std::move(executed).value());
    });
}

auto Database::copy_from(const std::string_view sql, std::istream& input, Session& session)
    -> base::Result<std::size_t> {
    return api_boundary("copy rows", [&]() -> base::Result<std::size_t> {
        if (implementation_ == nullptr) {
            return base::fail(unavailable("database is not open", "copy rows"));
        }
        if (!session.valid()) {
            return base::fail(unavailable("session has been closed", "copy rows"));
        }
        return implementation_->database.copy_from(sql, input, session.implementation_->session);
    });
}

auto Database::copy_from_rows(const std::string_view sql, CopyRowReader reader, Session& session)
    -> base::Result<std::size_t> {
    return api_boundary("copy typed rows", [&]() -> base::Result<std::size_t> {
        if (implementation_ == nullptr) {
            return base::fail(unavailable("database is not open", "copy typed rows"));
        }
        if (!session.valid()) {
            return base::fail(unavailable("session has been closed", "copy typed rows"));
        }
        return implementation_->database
            .copy_from_rows(sql, std::move(reader), session.implementation_->session);
    });
}

auto Database::load_into_empty_from(
    const std::string_view sql,
    std::istream& input,
    const std::uint64_t source_size_bytes,
    Session& session
) -> base::Result<std::size_t> {
    return api_boundary("load rows", [&]() -> base::Result<std::size_t> {
        if (implementation_ == nullptr) {
            return base::fail(unavailable("database is not open", "load rows"));
        }
        if (!session.valid()) {
            return base::fail(unavailable("session has been closed", "load rows"));
        }
        return implementation_->database
            .load_into_empty_from(sql, input, source_size_bytes, session.implementation_->session);
    });
}

}  // namespace db::api
