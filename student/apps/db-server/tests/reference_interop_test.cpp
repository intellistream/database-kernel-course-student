#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <spawn.h>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <sys/wait.h>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/io/socket.hpp"
#include "db/protocol/rmdb_client.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/value.hpp"

#include "server_application.hpp"
#include "server_test_support.hpp"

extern char** environ;

namespace db::server {
namespace {

inline constexpr auto kProcessPollInterval = std::chrono::milliseconds{25};
inline constexpr auto kServerStartTimeout = std::chrono::seconds{10};
inline constexpr auto kHarnessFinishTimeout = std::chrono::seconds{60};
inline constexpr auto kTerminateGracePeriod = std::chrono::seconds{2};

struct EnvironmentSetting final {
    std::string_view name;
    std::string_view value;
};

auto interop_error(
    const base::ErrorCode code,
    std::string message,
    const std::string_view operation
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "tests.reference_interop",
        std::string{operation}
    );
}

auto has_environment_name(const std::string_view entry, const std::string_view name) noexcept
    -> bool {
    return entry.size() > name.size() && entry.substr(0U, name.size()) == name
           && entry[name.size()] == '=';
}

auto child_environment(const std::span<const EnvironmentSetting> settings)
    -> std::vector<std::string> {
    std::vector<std::string> environment;
    for (char** entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string_view text{*entry};
        const bool overridden = std::any_of(
            settings.begin(),
            settings.end(),
            [text](const EnvironmentSetting& setting) {
                return has_environment_name(text, setting.name);
            }
        );
        if (!overridden) {
            environment.emplace_back(text);
        }
    }
    environment.reserve(environment.size() + settings.size());
    for (const EnvironmentSetting& setting : settings) {
        environment.push_back(std::string{setting.name}.append("=").append(setting.value));
    }
    return environment;
}

auto writable_c_strings(std::vector<std::string>& strings) -> std::vector<char*> {
    std::vector<char*> pointers;
    pointers.reserve(strings.size() + 1U);
    for (std::string& value : strings) {
        pointers.push_back(value.data());
    }
    pointers.push_back(nullptr);
    return pointers;
}

auto child_status_text(const int status) -> std::string {
    if (WIFEXITED(status)) {
        return "exit code " + std::to_string(WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        return "signal " + std::to_string(WTERMSIG(status));
    }
    return "unrecognized wait status " + std::to_string(status);
}

class ChildProcess final {
public:
    explicit ChildProcess(const pid_t process_id) noexcept
        : process_id_{process_id} {}

    ChildProcess(const ChildProcess&) = delete;
    auto operator=(const ChildProcess&) -> ChildProcess& = delete;

    ChildProcess(ChildProcess&& other) noexcept
        : process_id_{std::exchange(other.process_id_, -1)} {}

    auto operator=(ChildProcess&& other) noexcept -> ChildProcess& {
        if (this != &other) {
            terminate_noexcept();
            process_id_ = std::exchange(other.process_id_, -1);
        }
        return *this;
    }

    ~ChildProcess() {
        terminate_noexcept();
    }

    auto try_reap() -> base::Result<std::optional<int>> {
        if (process_id_ <= 0) {
            return base::fail(interop_error(
                base::ErrorCode::kInvalidArgument,
                "child process is no longer running",
                "poll child"
            ));
        }
        int status = 0;
        while (true) {
            const pid_t result = ::waitpid(process_id_, &status, WNOHANG);
            if (result == process_id_) {
                process_id_ = -1;
                return std::optional<int>{status};
            }
            if (result == 0) {
                return std::optional<int>{};
            }
            if (errno == EINTR) {
                continue;
            }
            const std::error_code error{errno, std::generic_category()};
            process_id_ = -1;
            return base::fail(
                base::Error{
                    base::ErrorCode::kIo,
                    "waitpid while polling child failed",
                    error,
                }
                    .with_context("tests.reference_interop", "poll child")
            );
        }
    }

    auto wait_for(const std::chrono::milliseconds timeout) -> base::Result<int> {
        if (timeout.count() <= 0) {
            return base::fail(interop_error(
                base::ErrorCode::kInvalidArgument,
                "child wait timeout must be positive",
                "wait for child"
            ));
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            auto reaped = try_reap();
            if (!reaped) {
                return base::fail(std::move(reaped).error());
            }
            if (reaped->has_value()) {
                return reaped->value();
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                terminate_noexcept();
                return base::fail(interop_error(
                    base::ErrorCode::kUnavailable,
                    "child process did not finish before its deadline",
                    "wait for child"
                ));
            }
            std::this_thread::sleep_for(
                std::min(
                    kProcessPollInterval,
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                )
            );
        }
    }

private:
    void terminate_noexcept() noexcept {
        if (process_id_ <= 0) {
            return;
        }
        if (::kill(process_id_, SIGTERM) != 0 && errno != ESRCH) {
            // A test teardown cannot safely report another failure. Reaping is
            // still attempted below so that this fixture never leaves a child.
        }

        const auto deadline = std::chrono::steady_clock::now() + kTerminateGracePeriod;
        while (std::chrono::steady_clock::now() < deadline) {
            int status = 0;
            const pid_t result = ::waitpid(process_id_, &status, WNOHANG);
            if (result == process_id_ || (result < 0 && errno == ECHILD)) {
                process_id_ = -1;
                return;
            }
            if (result < 0 && errno != EINTR) {
                process_id_ = -1;
                return;
            }
            std::this_thread::sleep_for(kProcessPollInterval);
        }

        if (::kill(process_id_, SIGKILL) != 0 && errno != ESRCH) {
            process_id_ = -1;
            return;
        }
        int status = 0;
        while (::waitpid(process_id_, &status, 0) < 0 && errno == EINTR) {}
        process_id_ = -1;
    }

    pid_t process_id_{-1};
};

auto start_process(
    const std::string_view program,
    const std::span<const std::string_view> arguments,
    const std::span<const EnvironmentSetting> settings
) -> base::Result<ChildProcess> {
    std::vector<std::string> command;
    command.reserve(arguments.size() + 1U);
    command.emplace_back(program);
    for (const std::string_view argument : arguments) {
        command.emplace_back(argument);
    }
    std::vector<char*> argument_pointers = writable_c_strings(command);
    std::vector<std::string> environment = child_environment(settings);
    std::vector<char*> environment_pointers = writable_c_strings(environment);

    pid_t process_id = -1;
    const int spawn_result = ::posix_spawn(
        &process_id,
        command.front().c_str(),
        nullptr,
        nullptr,
        argument_pointers.data(),
        environment_pointers.data()
    );
    if (spawn_result != 0) {
        return base::fail(
            base::Error{
                base::ErrorCode::kIo,
                "could not start reference interoperability child",
                std::error_code{spawn_result, std::generic_category()},
            }
                .with_context("tests.reference_interop", "spawn child")
        );
    }
    return ChildProcess{process_id};
}

auto client_options() -> protocol::RmdbClientOptions {
    return protocol::RmdbClientOptions{
        .io_timeout = std::chrono::milliseconds{5000},
        .maximum_response_bytes = protocol::kMaximumRmdbFramePayloadSize,
    };
}

auto connect_when_ready(const io::TcpEndpoint& endpoint, ChildProcess& child)
    -> base::Result<protocol::RmdbClient> {
    const auto deadline = std::chrono::steady_clock::now() + kServerStartTimeout;
    std::string last_connect_error;
    while (true) {
        auto client = protocol::RmdbClient::connect(endpoint, client_options());
        if (client) {
            return client;
        }
        last_connect_error = client.error().to_string();

        auto reaped = child.try_reap();
        if (!reaped) {
            return base::fail(std::move(reaped).error());
        }
        if (reaped->has_value()) {
            return base::fail(interop_error(
                base::ErrorCode::kUnavailable,
                "reference server exited before accepting a client ("
                    + child_status_text(reaped->value()) + ")",
                "wait for reference server"
            ));
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return base::fail(interop_error(
                base::ErrorCode::kUnavailable,
                "reference server did not become ready: " + last_connect_error,
                "wait for reference server"
            ));
        }
        std::this_thread::sleep_for(
            std::min(
                kProcessPollInterval,
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
            )
        );
    }
}

auto expect_command(protocol::RmdbClient& client, const std::string_view sql) -> base::Status {
    auto response = client.execute(sql);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (response->terminal.kind != protocol::RmdbTerminalKind::kCommandOk
        || !response->columns.empty() || !response->rows.empty()) {
        return base::fail(interop_error(
            base::ErrorCode::kExecution,
            "expected COMMAND_OK for " + std::string{sql},
            "exercise C++ client"
        ));
    }
    return {};
}

auto expect_single_integer(
    protocol::RmdbClient& client,
    const std::string_view sql,
    const std::int64_t expected
) -> base::Status {
    auto response = client.execute(sql);
    if (!response) {
        return base::fail(std::move(response).error());
    }
    if (response->terminal.kind != protocol::RmdbTerminalKind::kResultEnd
        || response->rows.size() != 1U || response->rows.front().size() != 1U) {
        return base::fail(interop_error(
            base::ErrorCode::kExecution,
            "expected one integer result row for " + std::string{sql},
            "exercise C++ client"
        ));
    }
    const std::int64_t* const value = response->rows.front().front().as_integer();
    if (value == nullptr || *value != expected) {
        return base::fail(interop_error(
            base::ErrorCode::kExecution,
            "integer result differed for " + std::string{sql},
            "exercise C++ client"
        ));
    }
    return {};
}

auto run_cpp_client_scenario(protocol::RmdbClient& client) -> base::Status {
    auto command = expect_command(
        client,
        "CREATE TABLE cpp_client_items (id INT, label CHAR(16), amount FLOAT)"
    );
    if (!command) {
        return command;
    }
    command =
        expect_command(client, "CREATE UNIQUE INDEX cpp_client_items_pk ON cpp_client_items (id)");
    if (!command) {
        return command;
    }
    command = expect_command(client, "CREATE INDEX cpp_client_items(label)");
    if (!command) {
        return command;
    }

    const std::vector<protocol::RmdbPreparedStatement> statements{
        protocol::RmdbPreparedStatement::command(1U, "BEGIN"),
        protocol::RmdbPreparedStatement::command(
            2U,
            "INSERT INTO cpp_client_items VALUES($1,$2,$3)",
            {
                protocol::WireDataType::kInt32,
                protocol::WireDataType::kChar,
                protocol::WireDataType::kFloat32,
            }
        ),
        protocol::RmdbPreparedStatement::query(
            3U,
            "SELECT id, label, amount FROM cpp_client_items WHERE id=$1",
            {protocol::WireDataType::kInt32},
            {
                protocol::WireDataType::kInt32,
                protocol::WireDataType::kChar,
                protocol::WireDataType::kFloat32,
            }
        ),
        protocol::RmdbPreparedStatement::command(4U, "COMMIT"),
    };
    auto catalog = client.prepare_set(statements);
    if (!catalog) {
        return base::fail(std::move(catalog).error());
    }

    const std::vector<protocol::RmdbBatchOperation> successful_operations{
        protocol::RmdbBatchOperation{.statement_id = 1U, .parameters = {}},
        protocol::RmdbBatchOperation{
            .statement_id = 2U,
            .parameters =
                {
                    types::Value::integer(1),
                    types::Value::character("native"),
                    types::Value::floating(1.25F),
                },
        },
        protocol::RmdbBatchOperation{
            .statement_id = 3U,
            .parameters = {types::Value::integer(1)},
        },
        protocol::RmdbBatchOperation{.statement_id = 4U, .parameters = {}},
    };
    auto successful = client.execute_batch(*catalog, successful_operations);
    if (!successful) {
        return base::fail(std::move(successful).error());
    }
    if (!successful->terminal.is_success()
        || successful->executed_operation_count != successful_operations.size()
        || !successful->responses[2U].has_value()) {
        return base::fail(interop_error(
            base::ErrorCode::kExecution,
            "reference server returned an invalid successful prepared batch",
            "exercise C++ client"
        ));
    }
    const protocol::RmdbResponse& query = *successful->responses[2U];
    if (query.rows.size() != 1U || query.rows.front().size() != 3U
        || query.rows.front()[0U].as_integer() == nullptr
        || *query.rows.front()[0U].as_integer() != 1
        || query.rows.front()[1U].as_string() == nullptr
        || *query.rows.front()[1U].as_string() != "native"
        || query.rows.front()[2U].as_float() == nullptr
        || *query.rows.front()[2U].as_float() != 1.25F) {
        return base::fail(interop_error(
            base::ErrorCode::kExecution,
            "reference server returned different prepared query values",
            "exercise C++ client"
        ));
    }

    const std::vector<protocol::RmdbBatchOperation> duplicate_operations{
        protocol::RmdbBatchOperation{.statement_id = 1U, .parameters = {}},
        protocol::RmdbBatchOperation{
            .statement_id = 2U,
            .parameters =
                {
                    types::Value::integer(2),
                    types::Value::character("first"),
                    types::Value::floating(2.0F),
                },
        },
        protocol::RmdbBatchOperation{
            .statement_id = 2U,
            .parameters =
                {
                    types::Value::integer(2),
                    types::Value::character("duplicate"),
                    types::Value::floating(2.0F),
                },
        },
        protocol::RmdbBatchOperation{.statement_id = 4U, .parameters = {}},
    };
    auto duplicate = client.execute_batch(*catalog, duplicate_operations);
    if (!duplicate) {
        return base::fail(std::move(duplicate).error());
    }
    if (duplicate->terminal.kind != protocol::RmdbBatchTerminalKind::kError
        || duplicate->terminal.failed_operation != 2U
        || duplicate->executed_operation_count != 2U) {
        return base::fail(interop_error(
            base::ErrorCode::kExecution,
            "reference server did not report the duplicate batch operation",
            "exercise C++ client"
        ));
    }

    auto count = expect_single_integer(client, "SELECT COUNT(*) FROM cpp_client_items", 1);
    if (!count) {
        return count;
    }
    command = expect_command(client, "CREATE TABLE cpp_client_empty (id INT)");
    if (!command) {
        return command;
    }
    auto sum = expect_single_integer(client, "SELECT SUM(id) FROM cpp_client_empty", 0);
    if (!sum) {
        return sum;
    }
    return expect_command(client, "CREATE STATIC_CHECKPOINT");
}

auto server_options(const std::filesystem::path& data_directory) -> ServerOptions {
    ServerOptions options;
    options.data_directory = data_directory;
    options.endpoint = io::TcpEndpoint::loopback(0U);
    options.buffer_pool_pages = 32U;
    options.tcp.maximum_concurrent_connections = 4U;
    options.tcp.listen_backlog = 8U;
    return options;
}

TEST(ReferenceInteropTest, OfficialReferenceClientDrivesCppServer) {
    auto root = test_support::TestDirectory::create("reference-client-drives-cpp");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    const std::string cargo_target = root->join("cargo-target").string();
    const std::string port = std::to_string(endpoint->port);
    const std::array<std::string_view, 6U> arguments{
        "run",
        "--offline",
        "--quiet",
        "--manifest-path",
        DB_REFERENCE_CLIENT_MANIFEST,
        "--",
    };
    std::vector<std::string_view> command_arguments{arguments.begin(), arguments.end()};
    command_arguments.push_back(port);
    const std::array<EnvironmentSetting, 2U> settings{
        EnvironmentSetting{.name = "CARGO_TARGET_DIR", .value = cargo_target},
        EnvironmentSetting{.name = "CARGO_NET_OFFLINE", .value = "true"},
    };
    auto harness = start_process(DB_REFERENCE_CARGO_EXECUTABLE, command_arguments, settings);
    ASSERT_TRUE(harness) << harness.error().to_string();
    auto status = harness->wait_for(
        std::chrono::duration_cast<std::chrono::milliseconds>(kHarnessFinishTimeout)
    );
    ASSERT_TRUE(status) << status.error().to_string();
    ASSERT_TRUE(WIFEXITED(status.value())) << child_status_text(status.value());
    EXPECT_EQ(WEXITSTATUS(status.value()), 0) << child_status_text(status.value());

    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(ReferenceInteropTest, CppClientDrivesOfficialReferenceServer) {
    auto root = test_support::TestDirectory::create("cpp-client-drives-reference");
    ASSERT_TRUE(root) << root.error().to_string();

    auto reservation = io::TcpListener::bind(io::TcpEndpoint::loopback(0U));
    ASSERT_TRUE(reservation) << reservation.error().to_string();
    auto endpoint = reservation->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    ASSERT_TRUE(reservation->close());

    const std::string endpoint_text = endpoint->to_string();
    const std::string data_directory = root->join("reference-data").string();
    const std::array<std::string_view, 1U> arguments{data_directory};
    const std::array<EnvironmentSetting, 2U> settings{
        EnvironmentSetting{.name = "FERRUM_ADDRESS", .value = endpoint_text},
        EnvironmentSetting{.name = "FERRUM_BUFFER_POOL_PAGES", .value = "32"},
    };
    auto server = start_process(DB_REFERENCE_SERVER_BINARY, arguments, settings);
    ASSERT_TRUE(server) << server.error().to_string();
    auto client = connect_when_ready(endpoint.value(), *server);
    ASSERT_TRUE(client) << client.error().to_string();
    const auto scenario = run_cpp_client_scenario(*client);
    EXPECT_TRUE(scenario) << scenario.error().to_string();
    EXPECT_TRUE(client->close());
}

}  // namespace
}  // namespace db::server
