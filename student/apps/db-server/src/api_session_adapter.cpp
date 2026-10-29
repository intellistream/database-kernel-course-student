#include "api_session_adapter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "db/api/session.hpp"
#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/types/data_type.hpp"
#include "db/types/value.hpp"

#include "api_result_adapter.hpp"
#include "stream_sql_compatibility.hpp"

namespace db::server {
namespace {

inline constexpr base::ExceptionBoundaryPolicy kAdapterExceptionBoundary{
    .component = "apps.db_server.adapter",
    .allocation_message = "RMDB API adapter could not allocate bounded application state",
    .exception_prefix = "unexpected exception escaped the RMDB API adapter: ",
    .unknown_exception_message = "unknown exception escaped the RMDB API adapter",
};

template <typename Callback>
auto adapter_boundary(const std::string_view operation, Callback&& callback) {
    return base::invoke_exception_boundary(
        kAdapterExceptionBoundary,
        operation,
        std::forward<Callback>(callback)
    );
}

class ApiSessionPort;

class ApiPreparedStatementPort final : public protocol::PreparedStatementPort {
public:
    ApiPreparedStatementPort(
        ApiSessionPort& session,
        api::PreparedStatement statement,
        std::vector<protocol::WireDataType> parameter_types,
        std::vector<protocol::ColumnMetadata> result_columns
    ) noexcept;

    auto parameter_types() const noexcept -> std::span<const protocol::WireDataType> override;
    auto result_columns() const noexcept -> std::span<const protocol::ColumnMetadata> override;
    auto execute(std::span<const types::Value> parameters)
        -> base::Result<protocol::ExecutionResult> override;

private:
    // RequestDispatcher destroys its connection-local prepared dictionary
    // before TcpServer closes and destroys the owning SessionPort.
    ApiSessionPort* session_{nullptr};
    api::PreparedStatement statement_;
    std::vector<protocol::WireDataType> parameter_types_;
    std::vector<protocol::ColumnMetadata> result_columns_;
};

class ApiSessionPort final : public protocol::SessionPort {
public:
    ApiSessionPort(std::shared_ptr<api::Database> database, api::Session session) noexcept
        : database_{std::move(database)},
          session_{std::move(session)} {}

    auto execute_stream(const std::string_view sql)
        -> base::Result<protocol::ExecutionResult> override {
        return adapter_boundary("execute stream", [&]() {
            return execute_stream_impl(sql);
        });
    }

    auto prepare(
        const std::string_view sql,
        const std::span<const protocol::WireDataType> parameter_types
    ) -> base::Result<std::unique_ptr<protocol::PreparedStatementPort>> override {
        return adapter_boundary("prepare statement", [&]() {
            if (!session_.has_value() || database_ == nullptr) {
                return base::Result<std::unique_ptr<protocol::PreparedStatementPort>>{base::fail(
                    internal::api_adapter_error(
                        base::ErrorCode::kUnavailable,
                        "RMDB session has been closed",
                        "prepare statement"
                    )
                )};
            }
            std::vector<types::DataType> api_types;
            api_types.reserve(parameter_types.size());
            for (const protocol::WireDataType type : parameter_types) {
                auto api_type = internal::api_parameter_data_type(type);
                if (!api_type) {
                    return base::Result<std::unique_ptr<protocol::PreparedStatementPort>>{
                        base::fail(std::move(api_type).error())
                    };
                }
                api_types.push_back(api_type.value());
            }
            auto prepared = database_->prepare(sql, api_types);
            if (!prepared) {
                return base::Result<std::unique_ptr<protocol::PreparedStatementPort>>{base::fail(
                    std::move(prepared).error().with_context(
                        "apps.db_server.adapter",
                        "prepare database SQL"
                    )
                )};
            }
            auto columns = internal::map_api_columns(prepared->result_columns());
            if (!columns) {
                return base::Result<std::unique_ptr<protocol::PreparedStatementPort>>{
                    base::fail(std::move(columns).error())
                };
            }
            return base::Result<std::unique_ptr<protocol::PreparedStatementPort>>{std::make_unique<
                ApiPreparedStatementPort>(
                *this,
                std::move(prepared).value(),
                std::vector<protocol::WireDataType>{parameter_types.begin(), parameter_types.end()},
                std::move(columns).value()
            )};
        });
    }

    auto begin() -> base::Status override {
        return adapter_boundary("begin transaction", [&]() {
            return execute_control(internal::StreamControl::kBegin);
        });
    }

    auto commit() -> base::Status override {
        return adapter_boundary("commit transaction", [&]() {
            return execute_control(internal::StreamControl::kCommit);
        });
    }

    auto rollback() -> base::Status override {
        return adapter_boundary("rollback transaction", [&]() {
            if (!session_.has_value()) {
                return base::Status{base::fail(
                    internal::api_adapter_error(
                        base::ErrorCode::kUnavailable,
                        "RMDB session has been closed",
                        "rollback transaction"
                    )
                )};
            }
            if (session_->state() == api::SessionState::kIdle) {
                return base::Status{};
            }
            return session_->rollback();
        });
    }

    auto close() -> base::Status override {
        return adapter_boundary("close session", [&]() {
            if (!session_.has_value()) {
                return base::Status{};
            }
            if (session_->state() != api::SessionState::kIdle) {
                auto rolled_back = session_->rollback();
                if (!rolled_back) {
                    return base::Status{base::fail(std::move(rolled_back).error())};
                }
            }
            auto closed = session_->close();
            if (!closed) {
                return base::Status{base::fail(std::move(closed).error())};
            }
            session_.reset();
            database_.reset();
            return base::Status{};
        });
    }

    auto execute_prepared(
        const api::PreparedStatement& statement,
        const std::span<const types::Value> parameters
    ) -> base::Result<protocol::ExecutionResult> {
        if (!session_.has_value() || database_ == nullptr) {
            return base::fail(
                internal::api_adapter_error(
                    base::ErrorCode::kUnavailable,
                    "RMDB session has been closed",
                    "execute prepared statement"
                )
            );
        }
        auto dispatched = database_->execute_prepared(*session_, statement, parameters);
        if (!dispatched) {
            return base::fail(
                std::move(dispatched)
                    .error()
                    .with_context("apps.db_server.adapter", "execute prepared statement")
            );
        }
        return internal::map_api_dispatch_result({}, std::move(dispatched).value());
    }

private:
    static constexpr std::size_t kLoadReadBufferBytes = std::size_t{256U} * std::size_t{1024U};

    static auto command_result() -> protocol::ExecutionResult {
        return protocol::ExecutionResult{};
    }

    auto execute_stream_impl(const std::string_view sql)
        -> base::Result<protocol::ExecutionResult> {
        if (!session_.has_value() || database_ == nullptr) {
            return base::fail(
                internal::api_adapter_error(
                    base::ErrorCode::kUnavailable,
                    "RMDB session has been closed",
                    "execute stream"
                )
            );
        }
        if (internal::has_multiple_top_level_statements(sql)) {
            return base::fail(
                internal::api_adapter_error(
                    base::ErrorCode::kUnsupported,
                    "RMDB EXEC_STREAM accepts exactly one SQL statement",
                    "execute stream"
                )
            );
        }
        const auto control = internal::parse_stream_control(sql);
        if (control.has_value()) {
            return execute_stream_control(*control);
        }
        if (internal::is_load_statement(sql)) {
            return execute_stream_load(sql);
        }
        return execute_stream_sql(sql);
    }

    auto execute_stream_control(const internal::StreamControl control)
        -> base::Result<protocol::ExecutionResult> {
        auto executed = execute_control(control);
        if (!executed) {
            return base::fail(std::move(executed).error());
        }
        return command_result();
    }

    auto execute_stream_load(const std::string_view sql)
        -> base::Result<protocol::ExecutionResult> {
        auto request = internal::parse_load_statement(sql);
        if (!request) {
            return base::fail(std::move(request).error());
        }
        auto loaded = execute_load(request.value());
        if (!loaded) {
            return base::fail(std::move(loaded).error());
        }
        return command_result();
    }

    auto execute_stream_sql(const std::string_view sql) -> base::Result<protocol::ExecutionResult> {
        auto rewrite = internal::rewrite_legacy_create_index(sql, next_legacy_index_id_);
        if (!rewrite) {
            return base::fail(std::move(rewrite).error());
        }
        std::string_view statement = sql;
        if (rewrite->has_value()) {
            statement = rewrite->value();
        }
        auto dispatched = database_->dispatch(*session_, statement);
        if (!dispatched) {
            return base::fail(
                std::move(dispatched)
                    .error()
                    .with_context("apps.db_server.adapter", "execute database SQL")
            );
        }
        return internal::map_api_dispatch_result(sql, std::move(dispatched).value());
    }

    auto execute_load(const internal::LoadRequest& request) -> base::Status {
        std::error_code source_error;
        const auto source_size = std::filesystem::file_size(request.source, source_error);
        if (source_error) {
            return base::fail(
                internal::api_adapter_error(
                    base::ErrorCode::kIo,
                    "could not inspect LOAD source " + request.source.string() + ": "
                        + source_error.message(),
                    "load CSV source"
                )
            );
        }
        if (source_size > std::numeric_limits<std::uint64_t>::max()) {
            return base::fail(
                internal::api_adapter_error(
                    base::ErrorCode::kOutOfRange,
                    "LOAD source size cannot be represented by the database API",
                    "load CSV source"
                )
            );
        }

        std::array<char, kLoadReadBufferBytes> buffer{};
        std::filebuf source_buffer;
        static_cast<void>(
            source_buffer.pubsetbuf(buffer.data(), static_cast<std::streamsize>(buffer.size()))
        );
        if (source_buffer.open(request.source, std::ios::in | std::ios::binary) == nullptr) {
            return base::fail(
                internal::api_adapter_error(
                    base::ErrorCode::kIo,
                    "could not open LOAD source " + request.source.string(),
                    "load CSV source"
                )
            );
        }
        std::istream input{&source_buffer};
        std::string header;
        if (!std::getline(input, header)) {
            const auto code =
                input.bad() ? base::ErrorCode::kIo : base::ErrorCode::kInvalidArgument;
            const std::string message =
                input.bad() ? "could not read LOAD header from " + request.source.string()
                            : "LOAD source " + request.source.string() + " has no CSV header";
            return base::fail(internal::api_adapter_error(code, message, "load CSV source"));
        }

        const std::string copy_sql = "COPY " + request.table + " FROM STDIN WITH (DELIMITER ',')";
        auto loaded = database_->load_into_empty_from(
            copy_sql,
            input,
            static_cast<std::uint64_t>(source_size),
            *session_
        );
        if (!loaded) {
            return base::fail(
                std::move(loaded).error().with_context("apps.db_server.adapter", "load CSV source")
            );
        }
        return {};
    }

    auto execute_control(const internal::StreamControl control) -> base::Status {
        if (!session_.has_value()) {
            return base::fail(
                internal::api_adapter_error(
                    base::ErrorCode::kUnavailable,
                    "RMDB session has been closed",
                    "execute transaction control"
                )
            );
        }
        switch (control) {
            case internal::StreamControl::kBegin:
                return session_->begin();
            case internal::StreamControl::kCommit:
                return session_->commit();
            case internal::StreamControl::kRollback:
                return rollback();
            case internal::StreamControl::kReadCommitted:
                return session_->set_isolation(api::SessionIsolation::kReadCommitted);
            case internal::StreamControl::kRepeatableRead:
                return session_->set_isolation(api::SessionIsolation::kRepeatableRead);
            case internal::StreamControl::kSerializable:
                return session_->set_isolation(api::SessionIsolation::kSerializable);
        }
        return base::fail(
            internal::api_adapter_error(
                base::ErrorCode::kInvariantViolation,
                "unknown RMDB transaction control reached the API adapter",
                "execute transaction control"
            )
        );
    }

    std::shared_ptr<api::Database> database_;
    std::optional<api::Session> session_;
    std::uint64_t next_legacy_index_id_{0U};
};

ApiPreparedStatementPort::ApiPreparedStatementPort(
    ApiSessionPort& session,
    api::PreparedStatement statement,
    std::vector<protocol::WireDataType> parameter_types,
    std::vector<protocol::ColumnMetadata> result_columns
) noexcept
    : session_{&session},
      statement_{std::move(statement)},
      parameter_types_{std::move(parameter_types)},
      result_columns_{std::move(result_columns)} {}

auto ApiPreparedStatementPort::parameter_types() const noexcept
    -> std::span<const protocol::WireDataType> {
    return parameter_types_;
}

auto ApiPreparedStatementPort::result_columns() const noexcept
    -> std::span<const protocol::ColumnMetadata> {
    return result_columns_;
}

auto ApiPreparedStatementPort::execute(const std::span<const types::Value> parameters)
    -> base::Result<protocol::ExecutionResult> {
    return adapter_boundary("execute prepared statement", [&]() {
        if (session_ == nullptr) {
            return base::Result<protocol::ExecutionResult>{base::fail(
                internal::api_adapter_error(
                    base::ErrorCode::kUnavailable,
                    "prepared statement no longer has a connection session",
                    "execute prepared statement"
                )
            )};
        }
        auto execution = session_->execute_prepared(statement_, parameters);
        if (!execution) {
            return base::Result<protocol::ExecutionResult>{
                base::fail(std::move(execution).error())
            };
        }
        if (execution->kind() == protocol::ExecutionKind::kQuery
            && !std::equal(
                execution->columns().begin(),
                execution->columns().end(),
                result_columns_.begin(),
                result_columns_.end()
            )) {
            return base::Result<protocol::ExecutionResult>{base::fail(
                internal::api_adapter_error(
                    base::ErrorCode::kInvariantViolation,
                    "database prepared result schema changed after PREPARE_SET",
                    "execute prepared statement"
                )
            )};
        }
        return execution;
    });
}

}  // namespace

ApiSessionFactory::ApiSessionFactory(std::shared_ptr<api::Database> database) noexcept
    : database_{std::move(database)} {}

auto ApiSessionFactory::open_session() -> base::Result<std::unique_ptr<protocol::SessionPort>> {
    return adapter_boundary("open session", [&]() {
        if (database_ == nullptr) {
            return base::Result<std::unique_ptr<protocol::SessionPort>>{base::fail(
                internal::api_adapter_error(
                    base::ErrorCode::kUnavailable,
                    "RMDB database has been released",
                    "open session"
                )
            )};
        }
        auto session = database_->new_session();
        if (!session) {
            return base::Result<std::unique_ptr<protocol::SessionPort>>{base::fail(
                std::move(session).error().with_context(
                    "apps.db_server.adapter",
                    "create database session"
                )
            )};
        }
        return base::Result<std::unique_ptr<protocol::SessionPort>>{
            std::make_unique<ApiSessionPort>(database_, std::move(session).value())
        };
    });
}

}  // namespace db::server
