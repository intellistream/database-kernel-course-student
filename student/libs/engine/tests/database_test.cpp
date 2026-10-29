#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include <sys/wait.h>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/engine/database.hpp"
#include "db/engine/session.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/value.hpp"
#include "db/wal/control_store.hpp"

namespace db::engine {
namespace {

auto recovery_test_config(const std::filesystem::path& path) -> DatabaseConfig {
    return DatabaseConfig{
        .data_directory = path,
        .buffer_pool_pages = 8U,
        .recovery_page_cache_pages = 4U,
        .wal_ring_pages = 8U,
    };
}

auto execute_ok(Database& database, Session& session, const std::string_view sql) -> bool {
    return static_cast<bool>(database.dispatch(session, sql));
}

void expect_inactive(Database& database) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(database.is_open());
    EXPECT_EQ(database.state(), DatabaseState::kShutdown);
    EXPECT_TRUE(database.data_directory().empty());
    EXPECT_EQ(database.system_identifier(), 0U);
    EXPECT_TRUE(database.close());
}

auto kill_self() -> int {
    if (::kill(::getpid(), SIGKILL) != 0) {
        return 99;
    }
    return 100;
}

template <typename ChildWork>
void expect_child_sigkill(const ChildWork& child_work) {
    const auto child = ::fork();
    ASSERT_NE(child, -1);
    if (child == 0) {
        ::_exit(child_work());
    }

    int status = 0;
    ASSERT_EQ(::waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFSIGNALED(status)) << "child status: " << status;
    EXPECT_EQ(WTERMSIG(status), SIGKILL);
}

void expect_single_int_row(
    Database& database,
    Session& session,
    const std::string_view sql,
    const std::int64_t expected
) {
    auto result = database.dispatch(session, sql);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->result_sets.size(), 1U);
    const auto& rows = result->result_sets.front();
    ASSERT_EQ(rows.size(), 1U);
    ASSERT_EQ(rows.rows().front().values().size(), 1U);
    EXPECT_EQ(rows.rows().front().values().front(), types::Value::integer(expected));
}

void expect_int_rows(
    Database& database,
    Session& session,
    const std::string_view sql,
    const std::vector<std::vector<std::int64_t>>& expected
) {
    auto result = database.dispatch(session, sql);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->result_sets.size(), 1U);
    const auto& rows = result->result_sets.front();
    ASSERT_EQ(rows.size(), expected.size());
    for (std::size_t row_index = 0U; row_index < expected.size(); ++row_index) {
        const auto& actual_values = rows.rows()[row_index].values();
        ASSERT_EQ(actual_values.size(), expected[row_index].size());
        for (std::size_t column_index = 0U; column_index < expected[row_index].size();
             ++column_index) {
            EXPECT_EQ(
                actual_values[column_index],
                types::Value::integer(expected[row_index][column_index])
            );
        }
    }
}

TEST(DatabaseTest, CreateCloseAndReopenOwnsDurableSubsystemLifecycle) {
    auto root = test_support::TestDirectory::create("engine-database-lifecycle");
    ASSERT_TRUE(root);

    DatabaseConfig config{
        .data_directory = root->path(),
        .buffer_pool_pages = 8U,
        .wal_ring_pages = 4U,
    };
    auto created = Database::create(config);
    ASSERT_TRUE(created) << created.error().to_string();
    const auto system_identifier = created->system_identifier();
    EXPECT_NE(system_identifier, 0U);
    EXPECT_TRUE(created->is_open());
    EXPECT_EQ(created->state(), DatabaseState::kInProduction);
    EXPECT_TRUE(std::filesystem::is_regular_file(root->join("control/control")));
    EXPECT_TRUE(std::filesystem::is_regular_file(root->join("catalog")));
    EXPECT_TRUE(std::filesystem::is_regular_file(root->join("xact/meta")));
    EXPECT_TRUE(std::filesystem::is_regular_file(root->join("database.lock")));

    auto session = created->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    ASSERT_TRUE(session->start_command());
    ASSERT_TRUE(session->finish_command(true));
    ASSERT_TRUE(session->close());

    ASSERT_TRUE(created->close());
    EXPECT_FALSE(created->is_open());
    EXPECT_EQ(created->state(), DatabaseState::kShutdown);

    auto control = wal::WalControlStore::open(root->join("control"));
    ASSERT_TRUE(control) << control.error().to_string();
    EXPECT_EQ(control->current().state, wal::DatabaseState::kShutdown);
    EXPECT_EQ(control->current().system_identifier, system_identifier);
    EXPECT_EQ(control->current().next_xid, base::Xid{2U});
    EXPECT_EQ(control->current().latest_completed_xid, base::Xid{1U});

    auto reopened = Database::open(config);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->system_identifier(), system_identifier);
    ASSERT_TRUE(reopened->close());
}

TEST(DatabaseTest, OpenOrCreateChoosesUnderTheDirectoryLockAndPreservesIdentity) {
    auto root = test_support::TestDirectory::create("engine-database-open-or-create");
    ASSERT_TRUE(root) << root.error().to_string();
    DatabaseConfig config{
        .data_directory = root->path(),
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };

    auto initialized = Database::open_or_create(config);
    ASSERT_TRUE(initialized) << initialized.error().to_string();
    const auto system_identifier = initialized->system_identifier();

    auto competing = Database::open_or_create(config);
    ASSERT_FALSE(competing);
    EXPECT_EQ(competing.error().code(), base::ErrorCode::kUnavailable);
    ASSERT_TRUE(initialized->close());

    auto reopened = Database::open_or_create(config);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->system_identifier(), system_identifier);
    ASSERT_TRUE(reopened->close());
}

TEST(DatabaseTest, OpenOrCreateRejectsAnExistingInvalidControlWithoutReplacingIt) {
    auto root = test_support::TestDirectory::create("engine-database-open-or-create-corrupt");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto control_directory = root->join("control");
    ASSERT_TRUE(std::filesystem::create_directory(control_directory));
    const auto control_path = control_directory / "control";
    {
        std::ofstream control{control_path, std::ios::binary};
        ASSERT_TRUE(control.is_open());
        control.put('x');
        ASSERT_TRUE(control.good());
    }

    DatabaseConfig config{
        .data_directory = root->path(),
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };
    auto rejected = Database::open_or_create(config);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kCorruption);
    EXPECT_EQ(std::filesystem::file_size(control_path), 1U);
}

TEST(DatabaseTest, MoveConstructionTransfersTheOnlyRetryableLifecycleOwner) {
    auto root = test_support::TestDirectory::create("engine-database-move");
    ASSERT_TRUE(root);
    DatabaseConfig config{
        .data_directory = root->path(),
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };
    auto created = Database::create(config);
    ASSERT_TRUE(created) << created.error().to_string();

    Database source = std::move(created).value();
    Database moved{std::move(source)};
    EXPECT_TRUE(moved.is_open());

    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inactive(source);

    auto competing = Database::open(config);
    ASSERT_FALSE(competing);
    EXPECT_EQ(competing.error().code(), base::ErrorCode::kUnavailable);
    ASSERT_TRUE(moved.close());
}

TEST(DatabaseTest, SecondProcessStyleOpenIsRejectedByDirectoryLock) {
    auto root = test_support::TestDirectory::create("engine-database-lock");
    ASSERT_TRUE(root);
    DatabaseConfig config{
        .data_directory = root->path(),
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };
    auto first = Database::create(config);
    ASSERT_TRUE(first) << first.error().to_string();

    auto second = Database::open(config);
    ASSERT_FALSE(second);
    EXPECT_EQ(second.error().code(), base::ErrorCode::kUnavailable);

    ASSERT_TRUE(first->close());
    auto after_release = Database::open(config);
    ASSERT_TRUE(after_release) << after_release.error().to_string();
    ASSERT_TRUE(after_release->close());
}

TEST(DatabaseTest, OpenAcceptsRecoverableStartupStates) {
    auto root = test_support::TestDirectory::create("engine-database-recovery-boundary");
    ASSERT_TRUE(root);
    DatabaseConfig config{
        .data_directory = root->path(),
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };
    auto created = Database::create(config);
    ASSERT_TRUE(created) << created.error().to_string();
    ASSERT_TRUE(created->close());

    auto control = wal::WalControlStore::open(root->join("control"));
    ASSERT_TRUE(control);
    auto current = control->current();
    current.generation += 1U;
    current.state = wal::DatabaseState::kStartingUp;
    ASSERT_TRUE(control->publish(current));

    auto opened = Database::open(config);
    ASSERT_TRUE(opened);
    EXPECT_EQ(opened->state(), DatabaseState::kInProduction);

    auto reopened = wal::WalControlStore::open(root->join("control"));
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->current().state, wal::DatabaseState::kInProduction);
    ASSERT_TRUE(opened->close());
}

TEST(DatabaseTest, OpenRecoversShuttingDownControlState) {
    auto root = test_support::TestDirectory::create("engine-database-recovery-shuttingdown");
    ASSERT_TRUE(root);
    DatabaseConfig config{
        .data_directory = root->path(),
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };
    auto created = Database::create(config);
    ASSERT_TRUE(created) << created.error().to_string();
    ASSERT_TRUE(created->close());

    auto control = wal::WalControlStore::open(root->join("control"));
    ASSERT_TRUE(control);
    auto current = control->current();
    current.generation += 1U;
    current.state = wal::DatabaseState::kShuttingDown;
    ASSERT_TRUE(control->publish(current));

    auto opened = Database::open(config);
    ASSERT_TRUE(opened) << opened.error().to_string();
    EXPECT_EQ(opened->state(), DatabaseState::kInProduction);
    ASSERT_TRUE(opened->close());
}

TEST(DatabaseTest, RecoversCommittedRowsAndAbortsDurableLoserAfterSigkill) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-loser");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());

    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 10;
        }
        auto writer = database->new_session();
        if (!writer) {
            return 11;
        }
        if (!execute_ok(*database, *writer, "CREATE TABLE crash_items (id INT)")) {
            return 12;
        }
        if (!execute_ok(
                *database,
                *writer,
                "CREATE UNIQUE INDEX crash_items_id_idx ON crash_items (id)"
            )) {
            return 13;
        }
        if (auto status = writer->begin(); !status) {
            return 14;
        }
        if (!execute_ok(*database, *writer, "INSERT INTO crash_items VALUES (9)")) {
            return 15;
        }

        // This independent commit forces the prior unfinished page WAL to a
        // durable prefix without giving the loser a completion record.
        auto committer = database->new_session();
        if (!committer) {
            return 16;
        }
        if (!execute_ok(*database, *committer, "INSERT INTO crash_items VALUES (7)")) {
            return 17;
        }
        return kill_self();
    });

    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    auto result = recovered->dispatch(*session, "SELECT id FROM crash_items ORDER BY id");
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->result_sets.size(), 1U);
    const auto& rows = result->result_sets.front();
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows.rows().front().values().front(), types::Value::integer(7));
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, RecoversFromPersistedCheckpointRedoStartAfterSigkill) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-checkpoint");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());

    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 20;
        }
        auto session = database->new_session();
        if (!session) {
            return 21;
        }
        if (!execute_ok(*database, *session, "CREATE TABLE checkpoint_items (id INT)")) {
            return 22;
        }
        if (!execute_ok(
                *database,
                *session,
                "CREATE UNIQUE INDEX checkpoint_items_id_idx ON checkpoint_items (id)"
            )) {
            return 23;
        }
        if (!execute_ok(*database, *session, "INSERT INTO checkpoint_items VALUES (1)")) {
            return 24;
        }
        if (!execute_ok(*database, *session, "CHECKPOINT")) {
            return 25;
        }
        if (!execute_ok(*database, *session, "INSERT INTO checkpoint_items VALUES (2)")) {
            return 26;
        }
        return kill_self();
    });

    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    auto result = recovered->dispatch(*session, "SELECT id FROM checkpoint_items ORDER BY id");
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->result_sets.size(), 1U);
    const auto& rows = result->result_sets.front();
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows.rows()[0U].values().front(), types::Value::integer(1));
    EXPECT_EQ(rows.rows()[1U].values().front(), types::Value::integer(2));
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, StaticCheckpointRecoversCommittedDmlAndLoserHeapAndIndexesIdempotently) {
    auto root = test_support::TestDirectory::create("engine-static-checkpoint-sigkill");
    ASSERT_TRUE(root);
    const auto config = recovery_test_config(root->path());
    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 71;
        }
        auto session = database->new_session();
        if (!session) {
            return 72;
        }
        for (const auto sql : {
                 "CREATE TABLE durable_items (id INT, v INT)",
                 "CREATE UNIQUE INDEX durable_id ON durable_items (id)",
                 "CREATE INDEX durable_value ON durable_items (v)",
                 "INSERT INTO durable_items VALUES (1, 10), (2, 20), (3, 30)",
                 "CREATE STATIC_CHECKPOINT",
                 "INSERT INTO durable_items VALUES (4, 40)",
                 "UPDATE durable_items SET v = 11 WHERE id = 1",
                 "DELETE FROM durable_items WHERE id = 2",
             }) {
            if (!execute_ok(*database, *session, sql)) {
                return 73;
            }
        }
        auto loser = database->new_session();
        if (!loser || !loser->begin()) {
            return 74;
        }
        for (const auto sql : {
                 "INSERT INTO durable_items VALUES (5, 50)",
                 "UPDATE durable_items SET v = 999 WHERE id = 3",
                 "DELETE FROM durable_items WHERE id = 4",
             }) {
            if (!execute_ok(*database, *loser, sql)) {
                return 75;
            }
        }
        // An unrelated committed write forces preceding loser WAL durable.
        if (!execute_ok(*database, *session, "INSERT INTO durable_items VALUES (6, 60)")) {
            return 76;
        }
        return kill_self();
    });
    for (std::size_t repetition = 0U; repetition < 2U; ++repetition) {
        expect_child_sigkill([config]() {
            auto database = Database::open(config);
            if (!database) {
                return 77;
            }
            auto session = database->new_session();
            if (!session) {
                return 78;
            }
            auto rows = database->dispatch(*session, "SELECT id, v FROM durable_items ORDER BY id");
            if (!rows || rows->result_sets.size() != 1U || rows->result_sets[0].size() != 4U) {
                return 79;
            }
            const auto result = rows->result_sets[0].rows();
            if (result[0].values()[0] != types::Value::integer(1)
                || result[0].values()[1] != types::Value::integer(11)
                || result[1].values()[0] != types::Value::integer(3)
                || result[1].values()[1] != types::Value::integer(30)
                || result[2].values()[0] != types::Value::integer(4)
                || result[2].values()[1] != types::Value::integer(40)
                || result[3].values()[0] != types::Value::integer(6)) {
                return 80;
            }
            return kill_self();
        });
    }
    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session);
    expect_int_rows(
        *recovered,
        *session,
        "SELECT id, v FROM durable_items ORDER BY id",
        {{1, 11}, {3, 30}, {4, 40}, {6, 60}}
    );
    expect_int_rows(*recovered, *session, "SELECT id FROM durable_items WHERE v = 30", {{3}});
    expect_int_rows(*recovered, *session, "SELECT id FROM durable_items WHERE v = 999", {});
    expect_int_rows(*recovered, *session, "SELECT id FROM durable_items WHERE id = 5", {});
    expect_int_rows(*recovered, *session, "SELECT v FROM durable_items WHERE id = 4", {{40}});
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, RepeatsSigkillRecoveryWithoutReusingCommittedState) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-loop");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());
    {
        auto database = Database::create(config);
        ASSERT_TRUE(database) << database.error().to_string();
        auto session = database->new_session();
        ASSERT_TRUE(session) << session.error().to_string();
        ASSERT_TRUE(execute_ok(*database, *session, "CREATE TABLE crash_loop_items (id INT)"));
        ASSERT_TRUE(session->close());
        ASSERT_TRUE(database->close());
    }

    for (std::int64_t expected = 1; expected <= 3; ++expected) {
        expect_child_sigkill([config, expected]() {
            auto database = Database::open(config);
            if (!database) {
                return 30;
            }
            auto session = database->new_session();
            if (!session) {
                return 31;
            }
            const auto sql =
                "INSERT INTO crash_loop_items VALUES (" + std::to_string(expected) + ")";
            if (!execute_ok(*database, *session, sql)) {
                return 32;
            }
            return kill_self();
        });

        auto recovered = Database::open(config);
        ASSERT_TRUE(recovered) << recovered.error().to_string();
        auto session = recovered->new_session();
        ASSERT_TRUE(session) << session.error().to_string();
        expect_single_int_row(
            *recovered,
            *session,
            "SELECT COUNT(*) FROM crash_loop_items",
            expected
        );
        ASSERT_TRUE(session->close());
        ASSERT_TRUE(recovered->close());
    }
}

TEST(DatabaseTest, RecoversTimestampCatalogHeapAndIndexAfterSigkill) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-timestamp");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());

    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 40;
        }
        auto session = database->new_session();
        if (!session) {
            return 41;
        }
        if (!execute_ok(
                *database,
                *session,
                "CREATE TABLE recovery_events (id INT, happened_at TIMESTAMP(4))"
            )) {
            return 42;
        }
        if (!execute_ok(
                *database,
                *session,
                "CREATE INDEX recovery_events_happened_at ON recovery_events (happened_at)"
            )) {
            return 43;
        }
        if (!execute_ok(
                *database,
                *session,
                "INSERT INTO recovery_events VALUES (1, '2000-01-01 00:00:00.12345')"
            )) {
            return 44;
        }
        return kill_self();
    });

    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    auto result = recovered->dispatch(
        *session,
        "SELECT happened_at FROM recovery_events "
        "WHERE happened_at = '2000-01-01 00:00:00.1235'"
    );
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->result_sets.size(), 1U);
    const auto& rows = result->result_sets.front();
    ASSERT_EQ(rows.size(), 1U);
    ASSERT_EQ(rows.rows().front().values().size(), 1U);
    EXPECT_EQ(
        rows.rows().front().values().front(),
        types::Value::timestamp(types::TimestampValue{123'500}, types::TypeModifier{4})
    );
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, RecoversUnfinishedMultiRelationTransactionAfterSigkill) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-multi-relation");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());

    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 50;
        }
        auto setup = database->new_session();
        if (!setup) {
            return 51;
        }
        if (!execute_ok(*database, *setup, "CREATE TABLE district (id INT, next_order_id INT)")
            || !execute_ok(*database, *setup, "CREATE TABLE stock (item_id INT, quantity INT)")
            || !execute_ok(*database, *setup, "CREATE TABLE orders (order_id INT, customer_id INT)")
            || !execute_ok(*database, *setup, "CREATE UNIQUE INDEX orders_id ON orders (order_id)")
            || !execute_ok(*database, *setup, "CREATE TABLE recovery_witness (id INT)")
            || !execute_ok(*database, *setup, "INSERT INTO district VALUES (1, 10)")
            || !execute_ok(*database, *setup, "INSERT INTO stock VALUES (7, 50)")) {
            return 52;
        }
        auto loser = database->new_session();
        if (!loser || !loser->begin()
            || !execute_ok(*database, *loser, "UPDATE district SET next_order_id = 11 WHERE id = 1")
            || !execute_ok(*database, *loser, "UPDATE stock SET quantity = 40 WHERE item_id = 7")
            || !execute_ok(*database, *loser, "INSERT INTO orders VALUES (10, 3)")) {
            return 53;
        }
        auto committer = database->new_session();
        if (!committer
            || !execute_ok(*database, *committer, "INSERT INTO recovery_witness VALUES (1)")) {
            return 54;
        }
        return kill_self();
    });

    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    expect_single_int_row(
        *recovered,
        *session,
        "SELECT next_order_id FROM district WHERE id = 1",
        10
    );
    expect_single_int_row(*recovered, *session, "SELECT quantity FROM stock WHERE item_id = 7", 50);
    expect_single_int_row(*recovered, *session, "SELECT COUNT(*) FROM orders", 0);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, RecoversWinnerAndDiscardsIndexedLoserAfterSigkill) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-winner-loser");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());

    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 60;
        }
        auto setup = database->new_session();
        if (!setup) {
            return 61;
        }
        if (!execute_ok(*database, *setup, "CREATE TABLE district (id INT, next_order_id INT)")
            || !execute_ok(*database, *setup, "CREATE TABLE orders (order_id INT, customer_id INT)")
            || !execute_ok(
                *database,
                *setup,
                "CREATE TABLE order_line (order_id INT, line_no INT, item_id INT)"
            )
            || !execute_ok(*database, *setup, "CREATE UNIQUE INDEX orders_id ON orders (order_id)")
            || !execute_ok(
                *database,
                *setup,
                "CREATE UNIQUE INDEX order_line_key ON order_line (order_id, line_no)"
            )
            || !execute_ok(*database, *setup, "CREATE TABLE recovery_witness (id INT)")
            || !execute_ok(*database, *setup, "INSERT INTO district VALUES (1, 4)")) {
            return 62;
        }
        auto winner = database->new_session();
        if (!winner || !winner->begin()
            || !execute_ok(*database, *winner, "UPDATE district SET next_order_id = 5 WHERE id = 1")
            || !execute_ok(*database, *winner, "INSERT INTO orders VALUES (4, 40)")
            || !execute_ok(*database, *winner, "INSERT INTO order_line VALUES (4, 1, 11)")
            || !execute_ok(*database, *winner, "INSERT INTO order_line VALUES (4, 2, 12)")
            || !winner->commit()) {
            return 63;
        }
        auto loser = database->new_session();
        if (!loser || !loser->begin()
            || !execute_ok(*database, *loser, "UPDATE district SET next_order_id = 6 WHERE id = 1")
            || !execute_ok(*database, *loser, "INSERT INTO orders VALUES (5, 50)")
            || !execute_ok(*database, *loser, "INSERT INTO order_line VALUES (5, 1, 13)")) {
            return 64;
        }
        auto committer = database->new_session();
        if (!committer
            || !execute_ok(*database, *committer, "INSERT INTO recovery_witness VALUES (1)")) {
            return 65;
        }
        return kill_self();
    });

    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    expect_single_int_row(
        *recovered,
        *session,
        "SELECT next_order_id FROM district WHERE id = 1",
        5
    );
    expect_int_rows(
        *recovered,
        *session,
        "SELECT customer_id FROM orders WHERE order_id = 4",
        {{40}}
    );
    expect_int_rows(
        *recovered,
        *session,
        "SELECT line_no, item_id FROM order_line WHERE order_id = 4 ORDER BY line_no",
        {{1, 11}, {2, 12}}
    );
    expect_single_int_row(
        *recovered,
        *session,
        "SELECT COUNT(*) FROM orders WHERE order_id = 5",
        0
    );
    expect_single_int_row(
        *recovered,
        *session,
        "SELECT COUNT(*) FROM order_line WHERE order_id = 5",
        0
    );
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, RecoversUniqueKeyReuseAfterAbortedEntryCleanupAndSigkill) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-unique-reuse");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());

    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 70;
        }
        auto setup = database->new_session();
        if (!setup
            || !execute_ok(*database, *setup, "CREATE TABLE unique_retry (id INT, payload INT)")
            || !execute_ok(
                *database,
                *setup,
                "CREATE UNIQUE INDEX unique_retry_id ON unique_retry (id)"
            )) {
            return 71;
        }
        auto aborted = database->new_session();
        if (!aborted || !aborted->begin()
            || !execute_ok(*database, *aborted, "INSERT INTO unique_retry VALUES (7, 10)")
            || !aborted->rollback()) {
            return 72;
        }
        auto winner = database->new_session();
        if (!winner || !winner->begin()
            || !execute_ok(*database, *winner, "INSERT INTO unique_retry VALUES (7, 20)")
            || !winner->commit()) {
            return 73;
        }
        return kill_self();
    });

    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    expect_single_int_row(
        *recovered,
        *session,
        "SELECT payload FROM unique_retry WHERE id = 7",
        20
    );
    auto duplicate = recovered->dispatch(*session, "INSERT INTO unique_retry VALUES (7, 30)");
    ASSERT_FALSE(duplicate);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, RecoversUncommittedIndexedDeletesAfterSigkill) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-index-delete");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());

    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 80;
        }
        auto setup = database->new_session();
        if (!setup
            || !execute_ok(*database, *setup, "CREATE TABLE indexed_rows (id INT, payload INT)")
            || !execute_ok(
                *database,
                *setup,
                "CREATE UNIQUE INDEX indexed_rows_id ON indexed_rows (id)"
            )
            || !execute_ok(*database, *setup, "CREATE TABLE recovery_witness (id INT)")) {
            return 81;
        }
        for (std::int64_t id = 1; id <= 24; ++id) {
            const auto sql = "INSERT INTO indexed_rows VALUES (" + std::to_string(id) + ", "
                             + std::to_string(id * 10) + ")";
            if (!execute_ok(*database, *setup, sql)) {
                return 82;
            }
        }
        auto loser = database->new_session();
        if (!loser || !loser->begin()
            || !execute_ok(*database, *loser, "DELETE FROM indexed_rows WHERE id = 5")
            || !execute_ok(
                *database,
                *loser,
                "DELETE FROM indexed_rows WHERE id >= 10 AND id < 15"
            )) {
            return 83;
        }
        auto committer = database->new_session();
        if (!committer
            || !execute_ok(*database, *committer, "INSERT INTO recovery_witness VALUES (1)")) {
            return 84;
        }
        return kill_self();
    });

    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    expect_single_int_row(*recovered, *session, "SELECT COUNT(*) FROM indexed_rows", 24);
    std::vector<std::vector<std::int64_t>> expected_rows;
    for (std::int64_t id = 4; id <= 15; ++id) {
        expected_rows.push_back({id});
    }
    expect_int_rows(
        *recovered,
        *session,
        "SELECT id FROM indexed_rows WHERE id >= 4 AND id <= 15 ORDER BY id",
        expected_rows
    );
    expect_single_int_row(
        *recovered,
        *session,
        "SELECT payload FROM indexed_rows WHERE id = 5",
        50
    );
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, RecoversCommittedAbortedAndInflightTransactionsAfterSigkill) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-three-statuses");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());

    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 90;
        }
        auto setup = database->new_session();
        if (!setup || !execute_ok(*database, *setup, "CREATE TABLE state (id INT, value INT)")
            || !execute_ok(*database, *setup, "CREATE UNIQUE INDEX state_id ON state (id)")
            || !execute_ok(*database, *setup, "CREATE TABLE recovery_witness (id INT)")) {
            return 91;
        }
        for (std::int64_t id = 1; id <= 3; ++id) {
            const auto sql = "INSERT INTO state VALUES (" + std::to_string(id) + ", "
                             + std::to_string(id * 10) + ")";
            if (!execute_ok(*database, *setup, sql)) {
                return 92;
            }
        }
        auto committed = database->new_session();
        if (!committed || !committed->begin()
            || !execute_ok(*database, *committed, "INSERT INTO state VALUES (4, 40)")
            || !committed->commit()) {
            return 93;
        }
        auto aborted = database->new_session();
        if (!aborted || !aborted->begin()
            || !execute_ok(*database, *aborted, "UPDATE state SET value = 11 WHERE id = 1")
            || !aborted->rollback()) {
            return 94;
        }
        auto inflight = database->new_session();
        if (!inflight || !inflight->begin()
            || !execute_ok(*database, *inflight, "UPDATE state SET value = 33 WHERE id = 3")
            || !execute_ok(*database, *inflight, "INSERT INTO state VALUES (5, 50)")
            || !execute_ok(*database, *inflight, "DELETE FROM state WHERE id = 5")) {
            return 95;
        }
        auto committer = database->new_session();
        if (!committer
            || !execute_ok(*database, *committer, "INSERT INTO recovery_witness VALUES (1)")) {
            return 96;
        }
        return kill_self();
    });

    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    expect_int_rows(
        *recovered,
        *session,
        "SELECT id, value FROM state ORDER BY id",
        {{1, 10}, {2, 20}, {3, 30}, {4, 40}}
    );
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, RecoversInterleavedRelationsAfterSigkill) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-interleaved");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());

    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 100;
        }
        auto setup = database->new_session();
        if (!setup
            || !execute_ok(*database, *setup, "CREATE TABLE warehouse (id INT, name CHAR(8))")
            || !execute_ok(*database, *setup, "CREATE TABLE district (id INT, warehouse_id INT)")
            || !execute_ok(*database, *setup, "CREATE TABLE customer (id INT, district_id INT)")
            || !execute_ok(*database, *setup, "CREATE TABLE orders (id INT, customer_id INT)")
            || !execute_ok(*database, *setup, "CREATE TABLE order_line (order_id INT, line_no INT)")
            || !execute_ok(*database, *setup, "CREATE UNIQUE INDEX orders_id ON orders (id)")
            || !execute_ok(
                *database,
                *setup,
                "CREATE UNIQUE INDEX order_line_key ON order_line (order_id, line_no)"
            )) {
            return 101;
        }
        auto loser = database->new_session();
        if (!loser || !loser->begin()
            || !execute_ok(*database, *loser, "INSERT INTO warehouse VALUES (1, 'north')")
            || !execute_ok(*database, *loser, "INSERT INTO district VALUES (10, 1)")) {
            return 102;
        }
        auto winner = database->new_session();
        if (!winner || !winner->begin()
            || !execute_ok(*database, *winner, "INSERT INTO orders VALUES (100, 7)")) {
            return 103;
        }
        if (!execute_ok(*database, *loser, "INSERT INTO customer VALUES (7, 10)")
            || !execute_ok(*database, *winner, "INSERT INTO order_line VALUES (100, 1)")
            || !execute_ok(*database, *winner, "INSERT INTO order_line VALUES (100, 2)")
            || !winner->commit()) {
            return 104;
        }
        return kill_self();
    });

    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    expect_single_int_row(*recovered, *session, "SELECT COUNT(*) FROM warehouse", 0);
    expect_single_int_row(*recovered, *session, "SELECT COUNT(*) FROM district", 0);
    expect_single_int_row(*recovered, *session, "SELECT COUNT(*) FROM customer", 0);
    expect_int_rows(*recovered, *session, "SELECT customer_id FROM orders WHERE id = 100", {{7}});
    expect_int_rows(
        *recovered,
        *session,
        "SELECT line_no FROM order_line WHERE order_id = 100 ORDER BY line_no",
        {{1}, {2}}
    );
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, RecoversCommittedUnloggedLoadAfterSigkill) {
    auto root = test_support::TestDirectory::create("engine-database-sigkill-unlogged-load");
    ASSERT_TRUE(root) << root.error().to_string();
    const auto config = recovery_test_config(root->path());

    expect_child_sigkill([config]() {
        auto database = Database::create(config);
        if (!database) {
            return 110;
        }
        auto session = database->new_session();
        if (!session
            || !execute_ok(*database, *session, "CREATE TABLE loaded (id INT, name CHAR(8))")
            || !execute_ok(*database, *session, "CREATE UNIQUE INDEX loaded_id ON loaded (id)")) {
            return 111;
        }
        std::istringstream input{"1,one\n2,two\n3,three\n"};
        auto loaded = database->load_into_empty_from(
            "COPY loaded FROM STDIN WITH (DELIMITER ',')",
            input,
            20U,
            *session
        );
        if (!loaded || loaded.value() != 3U) {
            return 112;
        }
        return kill_self();
    });

    auto recovered = Database::open(config);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    auto session = recovered->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    expect_single_int_row(*recovered, *session, "SELECT id FROM loaded WHERE id = 2", 2);
    expect_single_int_row(*recovered, *session, "SELECT COUNT(*) FROM loaded", 3);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(recovered->close());
}

TEST(DatabaseTest, InvalidConfigurationFailsBeforeCreatingSubsystems) {
    DatabaseConfig config{.data_directory = "", .buffer_pool_pages = 0U};
    auto result = Database::create(config);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kInvalidArgument);
}

}  // namespace
}  // namespace db::engine
