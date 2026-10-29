#include <barrier>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/protocol/rmdb_client.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/value.hpp"

#include "server_application.hpp"
#include "server_test_support.hpp"

namespace db::server {
namespace {

inline constexpr std::size_t kStressClientCount = 12U;
inline constexpr std::size_t kStressTransactionsPerClient = 6U;

auto client_options() -> protocol::RmdbClientOptions {
    return protocol::RmdbClientOptions{
        .io_timeout = std::chrono::milliseconds{5000},
        .maximum_response_bytes = protocol::kMaximumRmdbFramePayloadSize,
    };
}

auto server_options(const std::filesystem::path& data_directory) -> ServerOptions {
    ServerOptions options;
    options.data_directory = data_directory;
    options.endpoint = io::TcpEndpoint::loopback(0U);
    options.buffer_pool_pages = 32U;
    options.tcp.maximum_concurrent_connections = kStressClientCount + 4U;
    options.tcp.listen_backlog = kStressClientCount + 8U;
    return options;
}

void expect_command(protocol::RmdbClient& client, const std::string_view sql) {
    auto response = client.execute(sql);
    ASSERT_TRUE(response) << response.error().to_string();
    ASSERT_EQ(response->terminal.kind, protocol::RmdbTerminalKind::kCommandOk) << sql;
    ASSERT_TRUE(response->columns.empty()) << sql;
    ASSERT_TRUE(response->rows.empty()) << sql;
}

void expect_single_integer(
    protocol::RmdbClient& client,
    const std::string_view sql,
    const std::int64_t expected
) {
    auto response = client.execute(sql);
    ASSERT_TRUE(response) << response.error().to_string();
    ASSERT_EQ(response->terminal.kind, protocol::RmdbTerminalKind::kResultEnd) << sql;
    ASSERT_EQ(response->rows.size(), 1U) << sql;
    ASSERT_EQ(response->rows.front().size(), 1U) << sql;
    const std::int64_t* const value = response->rows.front().front().as_integer();
    ASSERT_NE(value, nullptr) << sql;
    EXPECT_EQ(*value, expected) << sql;
}

void expect_single_float(
    protocol::RmdbClient& client,
    const std::string_view sql,
    const float expected
) {
    auto response = client.execute(sql);
    ASSERT_TRUE(response) << response.error().to_string();
    ASSERT_EQ(response->terminal.kind, protocol::RmdbTerminalKind::kResultEnd) << sql;
    ASSERT_EQ(response->rows.size(), 1U) << sql;
    ASSERT_EQ(response->rows.front().size(), 1U) << sql;
    const float* const value = response->rows.front().front().as_float();
    ASSERT_NE(value, nullptr) << sql;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(*value), std::bit_cast<std::uint32_t>(expected)) << sql;
}

void expect_transaction_abort(protocol::RmdbClient& client, const std::string_view sql) {
    auto response = client.execute(sql);
    ASSERT_TRUE(response) << response.error().to_string();
    EXPECT_EQ(response->terminal.kind, protocol::RmdbTerminalKind::kTransactionAbort)
        << response->terminal.diagnostic;
}

auto stress_failure(std::string message) -> base::Status {
    return base::fail(base::Error{base::ErrorCode::kExecution, std::move(message)});
}

struct PreparedStressClient final {
    protocol::RmdbClient client;
    protocol::RmdbPreparedCatalog catalog;
    std::size_t client_index{0U};
};

auto run_stress_client(PreparedStressClient prepared, std::barrier<>& start_gate) -> base::Status {
    start_gate.arrive_and_wait();
    for (std::size_t transaction = 0U; transaction < kStressTransactionsPerClient; ++transaction) {
        const auto id = static_cast<std::int64_t>(
            prepared.client_index * kStressTransactionsPerClient + transaction + 1U
        );
        const std::vector<protocol::RmdbBatchOperation> operations{
            protocol::RmdbBatchOperation{.statement_id = 1U, .parameters = {}},
            protocol::RmdbBatchOperation{
                .statement_id = 2U,
                .parameters =
                    {
                        types::Value::integer(id),
                        types::Value::integer(id * 10),
                    },
            },
            protocol::RmdbBatchOperation{.statement_id = 3U, .parameters = {}},
        };
        auto response = prepared.client.execute_batch(prepared.catalog, operations);
        if (!response) {
            return base::fail(std::move(response).error());
        }
        if (!response->terminal.is_success()) {
            return stress_failure(
                std::string{"concurrent RMDB batch failed: "}.append(response->terminal.diagnostic)
            );
        }
    }
    return prepared.client.close();
}

TEST(RmdbLiveInteropTest, SupportsPreparedBatchesCompatibilityAndCsvLoad) {
    auto root = test_support::TestDirectory::create("rmdb-live-prepared");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto client = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(client) << client.error().to_string();
    expect_command(*client, "CREATE TABLE client_t (id INT, name CHAR(16), price FLOAT)");
    expect_command(*client, "CREATE UNIQUE INDEX client_t_pk ON client_t (id)");
    expect_command(*client, "CREATE INDEX client_t(name)");

    const std::vector<protocol::RmdbPreparedStatement> statements{
        protocol::RmdbPreparedStatement::command(1U, "BEGIN"),
        protocol::RmdbPreparedStatement::command(
            2U,
            "INSERT INTO client_t VALUES($1,$2,$3)",
            {
                protocol::WireDataType::kInt32,
                protocol::WireDataType::kChar,
                protocol::WireDataType::kFloat32,
            }
        ),
        protocol::RmdbPreparedStatement::query(
            3U,
            "SELECT id, name, price FROM client_t WHERE id=$1",
            {protocol::WireDataType::kInt32},
            {
                protocol::WireDataType::kInt32,
                protocol::WireDataType::kChar,
                protocol::WireDataType::kFloat32,
            }
        ),
        protocol::RmdbPreparedStatement::command(4U, "COMMIT"),
    };
    auto catalog = client->prepare_set(statements);
    ASSERT_TRUE(catalog) << catalog.error().to_string();
    EXPECT_TRUE(catalog->contains(3U));

    auto foreign_client = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(foreign_client) << foreign_client.error().to_string();
    const std::vector<protocol::RmdbBatchOperation> empty_operations{
        protocol::RmdbBatchOperation{.statement_id = 1U, .parameters = {}},
    };
    auto foreign_catalog = foreign_client->execute_batch(*catalog, empty_operations);
    ASSERT_FALSE(foreign_catalog);
    EXPECT_EQ(foreign_catalog.error().code(), base::ErrorCode::kInvalidArgument);
    ASSERT_TRUE(foreign_client->close());

    const std::vector<protocol::RmdbBatchOperation> successful_operations{
        protocol::RmdbBatchOperation{.statement_id = 1U, .parameters = {}},
        protocol::RmdbBatchOperation{
            .statement_id = 2U,
            .parameters =
                {
                    types::Value::integer(1),
                    types::Value::character("alpha"),
                    types::Value::floating(1.25F),
                },
        },
        protocol::RmdbBatchOperation{
            .statement_id = 3U,
            .parameters = {types::Value::integer(1)},
        },
        protocol::RmdbBatchOperation{.statement_id = 4U, .parameters = {}},
    };
    auto successful = client->execute_batch(*catalog, successful_operations);
    ASSERT_TRUE(successful) << successful.error().to_string();
    ASSERT_TRUE(successful->terminal.is_success());
    ASSERT_EQ(successful->executed_operation_count, successful_operations.size());
    ASSERT_TRUE(successful->responses[2U].has_value());
    const auto& query = *successful->responses[2U];
    ASSERT_EQ(query.rows.size(), 1U);
    ASSERT_EQ(query.rows.front().size(), 3U);
    const std::int64_t* const result_id = query.rows.front()[0U].as_integer();
    ASSERT_NE(result_id, nullptr);
    EXPECT_EQ(*result_id, 1);
    const std::string* const result_name = query.rows.front()[1U].as_string();
    ASSERT_NE(result_name, nullptr);
    EXPECT_EQ(*result_name, "alpha");
    const float* const result_price = query.rows.front()[2U].as_float();
    ASSERT_NE(result_price, nullptr);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(*result_price), std::bit_cast<std::uint32_t>(1.25F));

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
    auto duplicate = client->execute_batch(*catalog, duplicate_operations);
    ASSERT_TRUE(duplicate) << duplicate.error().to_string();
    EXPECT_EQ(duplicate->terminal.kind, protocol::RmdbBatchTerminalKind::kError);
    EXPECT_EQ(duplicate->terminal.failed_operation, 2U);
    EXPECT_EQ(duplicate->executed_operation_count, 2U);
    expect_single_integer(*client, "SELECT COUNT(*) FROM client_t", 1);

    expect_command(*client, "CREATE TABLE empty_items (id INT)");
    expect_single_integer(*client, "SELECT SUM(id) FROM empty_items", 0);
    expect_command(*client, "CREATE STATIC_CHECKPOINT");
    expect_command(*client, "CREATE TABLE load_items (id INT, label CHAR(16))");
    expect_command(*client, "CREATE UNIQUE INDEX load_items_pk ON load_items (id)");
    const auto csv_path = root->path() / "load_items.csv";
    std::ofstream csv{csv_path};
    ASSERT_TRUE(csv.is_open());
    csv << "id,label\n1,one\n2,two\n3,three\n";
    csv.close();
    expect_command(*client, "LOAD " + csv_path.string() + " INTO load_items");
    expect_single_integer(*client, "SELECT COUNT(*) FROM load_items", 3);
    expect_single_integer(*client, "SELECT id FROM load_items WHERE id = 3", 3);

    auto missing_load = client->execute("LOAD /tmp/missing.csv INTO client_t");
    ASSERT_TRUE(missing_load) << missing_load.error().to_string();
    EXPECT_EQ(missing_load->terminal.kind, protocol::RmdbTerminalKind::kError);
    ASSERT_TRUE(client->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, ConvertsSnapshotWriteConflictToReusableAbortTerminal) {
    auto root = test_support::TestDirectory::create("rmdb-live-snapshot");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE snapshot_items (id INT, value INT)");
    expect_command(*setup, "CREATE UNIQUE INDEX snapshot_items_pk ON snapshot_items (id)");
    expect_command(*setup, "INSERT INTO snapshot_items VALUES (1, 10)");
    ASSERT_TRUE(setup->close());

    auto winner = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto stale = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(winner) << winner.error().to_string();
    ASSERT_TRUE(stale) << stale.error().to_string();
    for (protocol::RmdbClient* current : {std::addressof(*winner), std::addressof(*stale)}) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION");
        expect_command(*current, "BEGIN");
    }
    expect_command(*winner, "UPDATE snapshot_items SET value=value+1 WHERE id=1");
    expect_command(*winner, "COMMIT");

    auto aborted = stale->execute("UPDATE snapshot_items SET value=value+9 WHERE id=1");
    ASSERT_TRUE(aborted) << aborted.error().to_string();
    EXPECT_EQ(aborted->terminal.kind, protocol::RmdbTerminalKind::kTransactionAbort)
        << aborted->terminal.diagnostic;
    expect_single_integer(*stale, "SELECT value FROM snapshot_items WHERE id=1", 11);

    // The same structured retry category must survive the DELETE mutation
    // path. Isolation remains connection-scoped after the first auto-abort.
    expect_command(*winner, "INSERT INTO snapshot_items VALUES (2, 20)");
    expect_command(*winner, "BEGIN");
    expect_command(*stale, "BEGIN");
    expect_command(*winner, "DELETE FROM snapshot_items WHERE id=2");
    expect_command(*winner, "COMMIT");
    auto deleted = stale->execute("DELETE FROM snapshot_items WHERE id=2");
    ASSERT_TRUE(deleted) << deleted.error().to_string();
    EXPECT_EQ(deleted->terminal.kind, protocol::RmdbTerminalKind::kTransactionAbort)
        << deleted->terminal.diagnostic;
    expect_single_integer(*stale, "SELECT COUNT(*) FROM snapshot_items", 1);

    ASSERT_TRUE(winner->close());
    ASSERT_TRUE(stale->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, ConvertsActiveSnapshotConflictToReusableAbortTerminal) {
    auto root = test_support::TestDirectory::create("rmdb-live-active-snapshot");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE active_snapshot_items (id INT, value INT)");
    expect_command(
        *setup,
        "CREATE UNIQUE INDEX active_snapshot_items_pk ON active_snapshot_items (id)"
    );
    expect_command(*setup, "INSERT INTO active_snapshot_items VALUES (1, 10)");
    ASSERT_TRUE(setup->close());

    auto winner = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto loser = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(winner) << winner.error().to_string();
    ASSERT_TRUE(loser) << loser.error().to_string();
    for (protocol::RmdbClient* current : {std::addressof(*winner), std::addressof(*loser)}) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION");
        expect_command(*current, "BEGIN");
    }

    expect_command(*winner, "UPDATE active_snapshot_items SET value=value+1 WHERE id=1");
    expect_transaction_abort(*loser, "DELETE FROM active_snapshot_items WHERE id=1");
    expect_command(*winner, "COMMIT");
    expect_single_integer(*loser, "SELECT value FROM active_snapshot_items WHERE id=1", 11);

    ASSERT_TRUE(winner->close());
    ASSERT_TRUE(loser->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, PreservesFloat32WireContractAcrossSqlOperations) {
    auto root = test_support::TestDirectory::create("rmdb-live-float-contract");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto client = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(client) << client.error().to_string();

    expect_command(*client, "CREATE TABLE float_relative (value FLOAT)");
    expect_command(*client, "INSERT INTO float_relative VALUES (1.0)");
    for (std::size_t update = 0U; update < 128U; ++update) {
        expect_command(*client, "UPDATE float_relative SET value=value+0.00000001");
    }
    expect_single_float(*client, "SELECT value FROM float_relative", 1.0F);

    expect_command(*client, "CREATE TABLE float_literals (value FLOAT)");
    expect_command(
        *client,
        "INSERT INTO float_literals VALUES (1.0000000595), (1.0000000595), (1.0000000595)"
    );
    expect_single_float(*client, "SELECT SUM(value) FROM float_literals", 3.0F);

    expect_command(*client, "CREATE TABLE float_accumulator (value FLOAT)");
    expect_command(*client, "INSERT INTO float_accumulator VALUES (16777216.0), (1.0), (1.0)");
    expect_single_float(*client, "SELECT SUM(value) FROM float_accumulator", 16'777'218.0F);

    ASSERT_TRUE(client->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, PreservesCommittedVisibilityAndSnapshotIsolation) {
    auto root = test_support::TestDirectory::create("rmdb-live-isolation");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE visible_items (id INT, value INT)");
    expect_command(*setup, "CREATE UNIQUE INDEX visible_items_pk ON visible_items (id)");
    expect_command(*setup, "INSERT INTO visible_items VALUES (1, 10)");

    auto writer = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto observer = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(writer) << writer.error().to_string();
    ASSERT_TRUE(observer) << observer.error().to_string();

    expect_command(*writer, "BEGIN");
    expect_command(*writer, "UPDATE visible_items SET value=11 WHERE id=1");
    expect_single_integer(*writer, "SELECT value FROM visible_items WHERE id=1", 11);
    expect_single_integer(*observer, "SELECT value FROM visible_items WHERE id=1", 10);
    expect_command(*writer, "ABORT");

    expect_command(*writer, "BEGIN");
    expect_command(*writer, "INSERT INTO visible_items VALUES (2, 20)");
    expect_single_integer(*writer, "SELECT COUNT(*) FROM visible_items", 2);
    expect_single_integer(*observer, "SELECT COUNT(*) FROM visible_items", 1);
    expect_command(*writer, "ABORT");

    expect_command(*writer, "BEGIN");
    expect_command(*writer, "DELETE FROM visible_items WHERE id=1");
    expect_single_integer(*writer, "SELECT COUNT(*) FROM visible_items", 0);
    expect_single_integer(*observer, "SELECT COUNT(*) FROM visible_items", 1);
    expect_command(*writer, "ABORT");

    expect_command(*setup, "CREATE TABLE snapshot_skew (id INT, value INT)");
    expect_command(*setup, "CREATE UNIQUE INDEX snapshot_skew_pk ON snapshot_skew (id)");
    expect_command(*setup, "INSERT INTO snapshot_skew VALUES (1, 10), (2, 20)");
    auto left = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto right = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(left) << left.error().to_string();
    ASSERT_TRUE(right) << right.error().to_string();
    for (protocol::RmdbClient* current : {std::addressof(*left), std::addressof(*right)}) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION");
        expect_command(*current, "BEGIN");
    }
    expect_single_integer(*left, "SELECT value FROM snapshot_skew WHERE id=2", 20);
    expect_single_integer(*right, "SELECT value FROM snapshot_skew WHERE id=1", 10);
    expect_command(*left, "UPDATE snapshot_skew SET value=value+1 WHERE id=1");
    expect_command(*right, "UPDATE snapshot_skew SET value=value+1 WHERE id=2");
    expect_command(*right, "COMMIT");
    expect_command(*left, "COMMIT");
    expect_single_integer(*left, "SELECT value FROM snapshot_skew WHERE id=1", 11);
    expect_single_integer(*left, "SELECT value FROM snapshot_skew WHERE id=2", 21);

    expect_command(*left, "BEGIN");
    expect_single_integer(*left, "SELECT value FROM snapshot_skew WHERE id=1", 11);
    expect_command(*right, "BEGIN");
    expect_command(*right, "UPDATE snapshot_skew SET value=value+1 WHERE id=1");
    expect_command(*right, "COMMIT");
    expect_single_integer(*left, "SELECT value FROM snapshot_skew WHERE id=1", 11);
    expect_command(*left, "COMMIT");

    ASSERT_TRUE(writer->close());
    ASSERT_TRUE(observer->close());
    ASSERT_TRUE(left->close());
    ASSERT_TRUE(right->close());
    ASSERT_TRUE(setup->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, ConvertsSerializableDangerousReadToReusableAbortTerminal) {
    auto root = test_support::TestDirectory::create("rmdb-live-serializable");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE serial_items (id INT, value INT)");
    expect_command(*setup, "CREATE UNIQUE INDEX serial_items_pk ON serial_items (id)");
    expect_command(*setup, "INSERT INTO serial_items VALUES (1, 10), (2, 20)");
    ASSERT_TRUE(setup->close());

    auto left = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto right = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(left) << left.error().to_string();
    ASSERT_TRUE(right) << right.error().to_string();
    for (protocol::RmdbClient* current : {std::addressof(*left), std::addressof(*right)}) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE");
        expect_command(*current, "BEGIN");
    }
    expect_single_integer(*right, "SELECT value FROM serial_items WHERE id=1", 10);
    expect_command(*left, "UPDATE serial_items SET value=value+1 WHERE id=1");
    expect_command(*right, "UPDATE serial_items SET value=value+1 WHERE id=2");
    auto aborted = left->execute("SELECT value FROM serial_items WHERE id=2");
    ASSERT_TRUE(aborted) << aborted.error().to_string();
    EXPECT_EQ(aborted->terminal.kind, protocol::RmdbTerminalKind::kTransactionAbort)
        << aborted->terminal.diagnostic;
    expect_command(*right, "COMMIT");
    expect_single_integer(*left, "SELECT value FROM serial_items WHERE id=2", 21);

    ASSERT_TRUE(left->close());
    ASSERT_TRUE(right->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, ConvertsCommittedSerializableDangerousReadToReusableAbortTerminal) {
    auto root = test_support::TestDirectory::create("rmdb-live-serializable-committed");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE serial_committed_items (id INT, value INT)");
    expect_command(
        *setup,
        "CREATE UNIQUE INDEX serial_committed_items_pk ON serial_committed_items (id)"
    );
    expect_command(*setup, "INSERT INTO serial_committed_items VALUES (1, 10), (2, 20)");
    ASSERT_TRUE(setup->close());

    auto left = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto right = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(left) << left.error().to_string();
    ASSERT_TRUE(right) << right.error().to_string();
    for (protocol::RmdbClient* current : {std::addressof(*left), std::addressof(*right)}) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE");
        expect_command(*current, "BEGIN");
    }
    expect_single_integer(*right, "SELECT value FROM serial_committed_items WHERE id=1", 10);
    expect_command(*left, "UPDATE serial_committed_items SET value=value+1 WHERE id=1");
    expect_command(*right, "UPDATE serial_committed_items SET value=value+1 WHERE id=2");
    expect_command(*right, "COMMIT");
    expect_transaction_abort(*left, "SELECT value FROM serial_committed_items WHERE id=2");

    ASSERT_TRUE(left->close());
    ASSERT_TRUE(right->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, PreservesSerializableThreeTransactionAcyclicChains) {
    auto root = test_support::TestDirectory::create("rmdb-live-serializable-chains");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE serial_heap_chain (id INT, value INT)");
    expect_command(*setup, "INSERT INTO serial_heap_chain VALUES (1, 10), (2, 20)");
    expect_command(*setup, "CREATE TABLE serial_index_chain (id INT, value INT)");
    expect_command(*setup, "CREATE UNIQUE INDEX serial_index_chain_pk ON serial_index_chain (id)");
    expect_command(*setup, "INSERT INTO serial_index_chain VALUES (1, 10), (2, 20)");
    ASSERT_TRUE(setup->close());

    auto head = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto middle = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto tail = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(head) << head.error().to_string();
    ASSERT_TRUE(middle) << middle.error().to_string();
    ASSERT_TRUE(tail) << tail.error().to_string();
    for (protocol::RmdbClient* current : {
             std::addressof(*head),
             std::addressof(*middle),
             std::addressof(*tail),
         }) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE");
    }

    const auto run_chain = [&head, &middle, &tail](const std::string_view table) {
        expect_command(*head, "BEGIN");
        expect_command(*middle, "BEGIN");
        expect_command(*tail, "BEGIN");
        expect_single_integer(
            *middle,
            std::string{"SELECT value FROM "}.append(table).append(" WHERE id=2"),
            20
        );
        expect_command(
            *tail,
            std::string{"UPDATE "}.append(table).append(" SET value=value+1 WHERE id=2")
        );
        expect_single_integer(
            *head,
            std::string{"SELECT value FROM "}.append(table).append(" WHERE id=1"),
            10
        );
        expect_command(*head, "COMMIT");
        expect_command(
            *middle,
            std::string{"UPDATE "}.append(table).append(" SET value=value+1 WHERE id=1")
        );
        expect_command(*middle, "COMMIT");
        expect_command(*tail, "COMMIT");
    };
    run_chain("serial_heap_chain");
    run_chain("serial_index_chain");

    ASSERT_TRUE(head->close());
    ASSERT_TRUE(middle->close());
    ASSERT_TRUE(tail->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, PreservesSerializableFilteredHeapScanAcyclicChain) {
    auto root = test_support::TestDirectory::create("rmdb-live-serializable-filtered-heap-chain");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE serial_filtered_heap_chain (id INT, value INT)");
    expect_command(*setup, "INSERT INTO serial_filtered_heap_chain VALUES (1, 10), (3, 30)");
    ASSERT_TRUE(setup->close());

    auto head = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto middle = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto tail = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(head) << head.error().to_string();
    ASSERT_TRUE(middle) << middle.error().to_string();
    ASSERT_TRUE(tail) << tail.error().to_string();
    for (protocol::RmdbClient* current : {
             std::addressof(*head),
             std::addressof(*middle),
             std::addressof(*tail),
         }) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE");
        expect_command(*current, "BEGIN");
    }

    // middle -> tail is a genuine rw edge on id=3. middle then writes id=2,
    // which is invisible to head. head reads only id=1. Recording head ->
    // middle merely because the heap scan physically encountered id=2 would
    // falsely doom middle even though the SQL predicate has no such edge.
    expect_single_integer(*middle, "SELECT value FROM serial_filtered_heap_chain WHERE id=3", 30);
    expect_command(*tail, "UPDATE serial_filtered_heap_chain SET value=31 WHERE id=3");
    expect_command(*tail, "COMMIT");
    expect_command(*middle, "INSERT INTO serial_filtered_heap_chain VALUES (2, 20)");
    expect_single_integer(*head, "SELECT value FROM serial_filtered_heap_chain WHERE id=1", 10);
    expect_command(*middle, "COMMIT");
    expect_command(*head, "COMMIT");

    ASSERT_TRUE(head->close());
    ASSERT_TRUE(middle->close());
    ASSERT_TRUE(tail->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, ConvertsSerializableThreeTransactionDangerousUpdateToAbort) {
    auto root = test_support::TestDirectory::create("rmdb-live-serializable-dangerous-update");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE serial_dangerous_update (id INT, value INT)");
    expect_command(
        *setup,
        "CREATE UNIQUE INDEX serial_dangerous_update_pk ON serial_dangerous_update (id)"
    );
    expect_command(*setup, "INSERT INTO serial_dangerous_update VALUES (1, 10), (2, 20)");
    ASSERT_TRUE(setup->close());

    auto head = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto middle = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto tail = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(head) << head.error().to_string();
    ASSERT_TRUE(middle) << middle.error().to_string();
    ASSERT_TRUE(tail) << tail.error().to_string();
    for (protocol::RmdbClient* current : {
             std::addressof(*head),
             std::addressof(*middle),
             std::addressof(*tail),
         }) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE");
        expect_command(*current, "BEGIN");
    }

    expect_single_integer(*middle, "SELECT value FROM serial_dangerous_update WHERE id=2", 20);
    expect_command(*tail, "UPDATE serial_dangerous_update SET value=value+1 WHERE id=2");
    expect_command(*tail, "COMMIT");
    expect_single_integer(*head, "SELECT value FROM serial_dangerous_update WHERE id=1", 10);
    expect_transaction_abort(
        *middle,
        "UPDATE serial_dangerous_update SET value=value+1 WHERE id=1"
    );
    expect_command(*head, "COMMIT");

    ASSERT_TRUE(head->close());
    ASSERT_TRUE(middle->close());
    ASSERT_TRUE(tail->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, ConvertsSerializableThreeTransactionDangerousInsertToAbort) {
    auto root = test_support::TestDirectory::create("rmdb-live-serializable-dangerous-insert");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE serial_dangerous_insert (id INT, value INT)");
    expect_command(
        *setup,
        "CREATE UNIQUE INDEX serial_dangerous_insert_pk ON serial_dangerous_insert (id)"
    );
    expect_command(*setup, "INSERT INTO serial_dangerous_insert VALUES (1, 10), (2, 20)");
    ASSERT_TRUE(setup->close());

    auto head = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto middle = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto tail = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(head) << head.error().to_string();
    ASSERT_TRUE(middle) << middle.error().to_string();
    ASSERT_TRUE(tail) << tail.error().to_string();
    for (protocol::RmdbClient* current : {
             std::addressof(*head),
             std::addressof(*middle),
             std::addressof(*tail),
         }) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE");
        expect_command(*current, "BEGIN");
    }

    expect_single_integer(*middle, "SELECT value FROM serial_dangerous_insert WHERE id=2", 20);
    expect_command(*tail, "UPDATE serial_dangerous_insert SET value=value+1 WHERE id=2");
    expect_command(*tail, "COMMIT");
    auto empty = head->execute("SELECT value FROM serial_dangerous_insert WHERE id=3");
    ASSERT_TRUE(empty) << empty.error().to_string();
    ASSERT_EQ(empty->terminal.kind, protocol::RmdbTerminalKind::kResultEnd);
    EXPECT_TRUE(empty->rows.empty());
    expect_transaction_abort(*middle, "INSERT INTO serial_dangerous_insert VALUES (3, 10)");
    expect_command(*head, "COMMIT");

    ASSERT_TRUE(head->close());
    ASSERT_TRUE(middle->close());
    ASSERT_TRUE(tail->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, ConvertsSerializableThreeTransactionDangerousDeleteToAbort) {
    auto root = test_support::TestDirectory::create("rmdb-live-serializable-dangerous-delete");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE serial_dangerous_delete (id INT, value INT)");
    expect_command(
        *setup,
        "CREATE UNIQUE INDEX serial_dangerous_delete_pk ON serial_dangerous_delete (id)"
    );
    expect_command(*setup, "INSERT INTO serial_dangerous_delete VALUES (1, 10), (2, 20)");
    ASSERT_TRUE(setup->close());

    auto head = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto middle = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto tail = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(head) << head.error().to_string();
    ASSERT_TRUE(middle) << middle.error().to_string();
    ASSERT_TRUE(tail) << tail.error().to_string();
    for (protocol::RmdbClient* current : {
             std::addressof(*head),
             std::addressof(*middle),
             std::addressof(*tail),
         }) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE");
        expect_command(*current, "BEGIN");
    }

    expect_single_integer(*middle, "SELECT value FROM serial_dangerous_delete WHERE id=2", 20);
    expect_command(*tail, "UPDATE serial_dangerous_delete SET value=value+1 WHERE id=2");
    expect_command(*tail, "COMMIT");
    expect_single_integer(*head, "SELECT value FROM serial_dangerous_delete WHERE id=1", 10);
    expect_transaction_abort(*middle, "DELETE FROM serial_dangerous_delete WHERE id=1");
    expect_command(*head, "COMMIT");

    ASSERT_TRUE(head->close());
    ASSERT_TRUE(middle->close());
    ASSERT_TRUE(tail->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, ConvertsSerializablePredicateMembershipUpdateToAbort) {
    auto root = test_support::TestDirectory::create("rmdb-live-serializable-predicate-membership");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE serial_predicate_membership (id INT, value INT)");
    expect_command(*setup, "INSERT INTO serial_predicate_membership VALUES (1, 10), (2, 20)");
    ASSERT_TRUE(setup->close());

    auto low = protocol::RmdbClient::connect(endpoint.value(), client_options());
    auto high = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(low) << low.error().to_string();
    ASSERT_TRUE(high) << high.error().to_string();
    for (protocol::RmdbClient* current : {std::addressof(*low), std::addressof(*high)}) {
        expect_command(*current, "SET TRANSACTION ISOLATION LEVEL SERIALIZABLE");
        expect_command(*current, "BEGIN");
    }

    expect_single_integer(*low, "SELECT value FROM serial_predicate_membership WHERE value<15", 10);
    expect_single_integer(
        *high,
        "SELECT value FROM serial_predicate_membership WHERE value>15",
        20
    );
    expect_command(*low, "UPDATE serial_predicate_membership SET value=1 WHERE id=2");
    expect_transaction_abort(*high, "UPDATE serial_predicate_membership SET value=30 WHERE id=1");
    expect_command(*low, "COMMIT");

    ASSERT_TRUE(low->close());
    ASSERT_TRUE(high->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

TEST(RmdbLiveInteropTest, CompletesConcurrentPreparedBatchesAndCleansConnections) {
    auto root = test_support::TestDirectory::create("rmdb-live-concurrency");
    ASSERT_TRUE(root) << root.error().to_string();
    auto application = ServerApplication::open(server_options(root->path()));
    ASSERT_TRUE(application) << application.error().to_string();
    const auto endpoint = application->local_endpoint();
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    server_test::RunningServer running{*application};

    auto setup = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(setup) << setup.error().to_string();
    expect_command(*setup, "CREATE TABLE stress_items (id INT, value INT)");
    expect_command(*setup, "CREATE UNIQUE INDEX stress_items_pk ON stress_items (id)");
    ASSERT_TRUE(setup->close());

    const std::vector<protocol::RmdbPreparedStatement> statements{
        protocol::RmdbPreparedStatement::command(1U, "BEGIN"),
        protocol::RmdbPreparedStatement::command(
            2U,
            "INSERT INTO stress_items VALUES($1,$2)",
            {protocol::WireDataType::kInt32, protocol::WireDataType::kInt32}
        ),
        protocol::RmdbPreparedStatement::command(3U, "COMMIT"),
    };
    std::vector<PreparedStressClient> prepared_clients;
    prepared_clients.reserve(kStressClientCount);
    for (std::size_t client_index = 0U; client_index < kStressClientCount; ++client_index) {
        auto client = protocol::RmdbClient::connect(endpoint.value(), client_options());
        ASSERT_TRUE(client) << client.error().to_string();
        auto catalog = client->prepare_set(statements);
        ASSERT_TRUE(catalog) << catalog.error().to_string();
        prepared_clients.push_back(
            PreparedStressClient{
                .client = std::move(client).value(),
                .catalog = std::move(catalog).value(),
                .client_index = client_index,
            }
        );
    }

    std::barrier start_gate{static_cast<std::ptrdiff_t>(prepared_clients.size())};
    std::vector<std::future<base::Status>> workers;
    workers.reserve(prepared_clients.size());
    for (PreparedStressClient& prepared : prepared_clients) {
        workers.push_back(
            std::async(std::launch::async, [prepared = std::move(prepared), &start_gate]() mutable {
                return run_stress_client(std::move(prepared), start_gate);
            })
        );
    }
    for (std::future<base::Status>& worker : workers) {
        const auto status = worker.get();
        ASSERT_TRUE(status) << status.error().to_string();
    }

    auto verifier = protocol::RmdbClient::connect(endpoint.value(), client_options());
    ASSERT_TRUE(verifier) << verifier.error().to_string();
    expect_single_integer(
        *verifier,
        "SELECT COUNT(*) FROM stress_items",
        static_cast<std::int64_t>(kStressClientCount * kStressTransactionsPerClient)
    );
    ASSERT_TRUE(verifier->close());
    const auto stopped = running.stop();
    ASSERT_TRUE(stopped) << stopped.error().to_string();
    ASSERT_TRUE(application->shutdown());
}

}  // namespace
}  // namespace db::server
