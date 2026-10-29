#include "server_application.hpp"

#include <charconv>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "db/api/database.hpp"
#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"

#include "api_session_adapter.hpp"

namespace db::server {
namespace {

inline constexpr base::ExceptionBoundaryPolicy kApplicationExceptionBoundary{
    .component = "apps.db_server",
    .allocation_message = "server application could not allocate bounded state",
    .exception_prefix = "unexpected exception escaped the server application: ",
    .unknown_exception_message = "unknown exception escaped the server application",
};

template <typename Callback>
auto application_boundary(const std::string_view operation, Callback&& callback) {
    return base::invoke_exception_boundary(
        kApplicationExceptionBoundary,
        operation,
        std::forward<Callback>(callback)
    );
}

auto application_error(
    const base::ErrorCode code,
    std::string message,
    const std::string_view operation
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "apps.db_server",
        std::string{operation}
    );
}

auto parse_positive_size(const std::string_view text, const std::string_view setting)
    -> base::Result<std::size_t> {
    if (text.empty()) {
        return base::fail(application_error(
            base::ErrorCode::kInvalidArgument,
            std::string{setting} + " must be a positive number of pages",
            "parse environment"
        ));
    }
    std::size_t value{0U};
    const char* const first = text.data();
    const char* const last = std::to_address(text.end());
    const auto [position, error] = std::from_chars(first, last, value);
    if (error != std::errc{} || position != last || value == 0U) {
        return base::fail(application_error(
            base::ErrorCode::kInvalidArgument,
            std::string{setting} + " must be a positive number of pages",
            "parse environment"
        ));
    }
    return value;
}

auto open_database(const ServerOptions& options) -> base::Result<std::shared_ptr<api::Database>> {
    if (options.buffer_pool_pages == 0U) {
        return base::fail(application_error(
            base::ErrorCode::kInvalidArgument,
            "server buffer pool must contain at least one page",
            "open database"
        ));
    }

    api::DatabaseConfig config{};
    config.data_directory = options.data_directory;
    config.buffer_pool_pages = options.buffer_pool_pages;

    auto database = api::Database::open_or_create(config);
    if (!database) {
        return base::fail(
            std::move(database).error().with_context("apps.db_server", "open database")
        );
    }
    return std::make_shared<api::Database>(std::move(database).value());
}

}  // namespace

struct ServerApplication::Impl final {
    explicit Impl(std::shared_ptr<api::Database> database_value)
        : database{std::move(database_value)},
          session_factory{std::make_unique<ApiSessionFactory>(database)} {}

    std::shared_ptr<api::Database> database;
    std::unique_ptr<ApiSessionFactory> session_factory;
    std::optional<protocol::TcpServer> server;
};

auto parse_server_options(const std::span<char*> arguments) -> base::Result<ServerOptions> {
    return application_boundary("parse arguments", [&]() -> base::Result<ServerOptions> {
        if (arguments.empty() || arguments.front() == nullptr) {
            return base::fail(application_error(
                base::ErrorCode::kInvalidArgument,
                "server arguments did not include an executable name",
                "parse arguments"
            ));
        }
        if (arguments.size() > 2U) {
            return base::fail(application_error(
                base::ErrorCode::kInvalidArgument,
                "db-server accepts at most one data-directory argument",
                "parse arguments"
            ));
        }

        ServerOptions options;
        if (arguments.size() == 2U) {
            if (arguments[1] == nullptr || std::string_view{arguments[1]}.empty()) {
                return base::fail(application_error(
                    base::ErrorCode::kInvalidArgument,
                    "server data-directory argument must not be empty",
                    "parse arguments"
                ));
            }
            options.data_directory = arguments[1];
        }
        // Startup reads immutable process configuration before workers exist.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (const char* const address = std::getenv("DB_ADDRESS"); address != nullptr) {
            auto endpoint = io::TcpEndpoint::parse(address);
            if (!endpoint) {
                return base::fail(
                    std::move(endpoint).error().with_context("apps.db_server", "parse DB_ADDRESS")
                );
            }
            options.endpoint = endpoint.value();
        }
        // Startup reads immutable process configuration before workers exist.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        if (const char* const pages = std::getenv("DB_BUFFER_POOL_PAGES"); pages != nullptr) {
            auto parsed = parse_positive_size(pages, "DB_BUFFER_POOL_PAGES");
            if (!parsed) {
                return base::fail(std::move(parsed).error());
            }
            options.buffer_pool_pages = parsed.value();
        }
        return options;
    });
}

auto ServerApplication::open(const ServerOptions& options) -> base::Result<ServerApplication> {
    return application_boundary("open server", [&]() -> base::Result<ServerApplication> {
        auto database = open_database(options);
        if (!database) {
            return base::fail(std::move(database).error());
        }
        auto implementation = std::make_unique<Impl>(std::move(database).value());
        auto server = protocol::TcpServer::bind(
            options.endpoint,
            *implementation->session_factory,
            options.tcp
        );
        if (!server) {
            const base::Error bind_error =
                std::move(server).error().with_context("apps.db_server", "bind TCP server");
            static_cast<void>(implementation->database->close());
            return base::fail(bind_error);
        }
        implementation->server.emplace(std::move(server).value());
        return ServerApplication{std::move(implementation)};
    });
}

ServerApplication::ServerApplication(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

ServerApplication::ServerApplication(ServerApplication&& other) noexcept = default;

ServerApplication::~ServerApplication() {
    static_cast<void>(shutdown());
}

auto ServerApplication::local_endpoint() const -> base::Result<io::TcpEndpoint> {
    return application_boundary("read local endpoint", [&]() -> base::Result<io::TcpEndpoint> {
        if (implementation_ == nullptr || !implementation_->server.has_value()) {
            return base::fail(application_error(
                base::ErrorCode::kUnavailable,
                "server application has been shut down",
                "read local endpoint"
            ));
        }
        return implementation_->server->local_endpoint();
    });
}

auto ServerApplication::serve_forever() -> base::Status {
    return application_boundary("serve forever", [&]() -> base::Status {
        if (implementation_ == nullptr || !implementation_->server.has_value()) {
            return base::fail(application_error(
                base::ErrorCode::kUnavailable,
                "server application has been shut down",
                "serve forever"
            ));
        }
        return implementation_->server->serve_forever();
    });
}

void ServerApplication::request_shutdown() noexcept {
    if (implementation_ != nullptr && implementation_->server.has_value()) {
        implementation_->server->request_shutdown();
    }
}

auto ServerApplication::shutdown() -> base::Status {
    return application_boundary("shutdown", [&]() -> base::Status {
        if (implementation_ == nullptr) {
            return {};
        }
        if (implementation_->server.has_value()) {
            implementation_->server->request_shutdown();
            implementation_->server.reset();
        }
        implementation_->session_factory.reset();
        if (implementation_->database == nullptr) {
            implementation_.reset();
            return base::fail(application_error(
                base::ErrorCode::kInvariantViolation,
                "server application lost its database before shutdown",
                "shutdown"
            ));
        }
        auto closed = implementation_->database->close();
        if (!closed) {
            return closed;
        }
        implementation_->database.reset();
        implementation_.reset();
        return {};
    });
}

}  // namespace db::server
