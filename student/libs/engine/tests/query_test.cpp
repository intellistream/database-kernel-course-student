#include <array>
#include <filesystem>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/engine/database.hpp"
#include "db/engine/dispatch.hpp"
#include "db/engine/session.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/value.hpp"
#include "db/wal/reader.hpp"

namespace db::engine {
namespace {

auto test_config(const std::filesystem::path& path) -> DatabaseConfig {
    return DatabaseConfig{
        .data_directory = path,
        .buffer_pool_pages = 8U,
        .wal_ring_pages = 8U,
    };
}

void expect_statement_ok(Database& database, Session& session, const std::string_view sql) {
    const auto result = database.dispatch(session, sql);
    ASSERT_TRUE(result) << result.error().to_string();
}

auto execute_query(Database& database, Session& session, const std::string_view sql) -> ResultSet {
    const auto result = database.dispatch(session, sql);
    EXPECT_TRUE(result) << result.error().to_string();
    if (!result || result->result_sets.empty()) {
        auto empty = ResultSet::create(ResultSchema{}, {});
        return std::move(empty).value();
    }
    return result->result_sets.front();
}

TEST(QueryTest, BuiltinDispatchExecutesIndexedQueriesAndDml) {
    auto root = test_support::TestDirectory::create("engine-query-dml");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();

    expect_statement_ok(*database, *session, "CREATE TABLE items (id INT, grp INT, score FLOAT)");
    expect_statement_ok(*database, *session, "CREATE UNIQUE INDEX items_id_idx ON items (id)");
    expect_statement_ok(*database, *session, "CREATE INDEX items_score_idx ON items (score)");
    expect_statement_ok(
        *database,
        *session,
        "INSERT INTO items VALUES (1, 1, 2.5), (2, 1, NULL), (3, 2, 4.0)"
    );

    const auto indexed =
        execute_query(*database, *session, "SELECT id, score FROM items WHERE id = 2");
    ASSERT_EQ(indexed.size(), 1U);
    ASSERT_EQ(indexed.schema().columns().size(), 2U);
    ASSERT_EQ(indexed.rows()[0].values().size(), 2U);
    EXPECT_EQ(indexed.rows()[0].values()[0], types::Value::integer(2));
    EXPECT_TRUE(indexed.rows()[0].values()[1].is_null());

    const auto indexed_null =
        execute_query(*database, *session, "SELECT id FROM items WHERE score IS NULL");
    ASSERT_EQ(indexed_null.size(), 1U);
    ASSERT_EQ(indexed_null.rows()[0].values().size(), 1U);
    EXPECT_EQ(indexed_null.rows()[0].values()[0], types::Value::integer(2));

    const auto grouped = execute_query(
        *database,
        *session,
        "SELECT grp, COUNT(*), SUM(score) FROM items GROUP BY grp ORDER BY grp"
    );
    ASSERT_EQ(grouped.size(), 2U);
    EXPECT_EQ(grouped.rows()[0].values()[0], types::Value::integer(1));
    EXPECT_EQ(grouped.rows()[0].values()[1], types::Value::integer(2));
    EXPECT_EQ(grouped.rows()[0].values()[2], types::Value::floating(2.5F));
    EXPECT_EQ(grouped.rows()[1].values()[0], types::Value::integer(2));
    EXPECT_EQ(grouped.rows()[1].values()[1], types::Value::integer(1));
    EXPECT_EQ(grouped.rows()[1].values()[2], types::Value::floating(4.0F));

    const auto updated =
        database->dispatch(*session, "UPDATE items SET score = score + 1 WHERE id = 3");
    ASSERT_TRUE(updated) << updated.error().to_string();
    EXPECT_EQ(updated->affected_row_count, 1U);

    const auto deleted = database->dispatch(*session, "DELETE FROM items WHERE id = 1");
    ASSERT_TRUE(deleted) << deleted.error().to_string();
    EXPECT_EQ(deleted->affected_row_count, 1U);

    const auto remaining =
        execute_query(*database, *session, "SELECT id, score FROM items ORDER BY id");
    ASSERT_EQ(remaining.size(), 2U);
    EXPECT_EQ(remaining.rows()[0].values()[0], types::Value::integer(2));
    EXPECT_EQ(remaining.rows()[1].values()[0], types::Value::integer(3));
    EXPECT_EQ(remaining.rows()[1].values()[1], types::Value::floating(5.0F));

    const auto duplicate = database->dispatch(*session, "INSERT INTO items VALUES (2, 9, 0.0)");
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kAlreadyExists);
    EXPECT_EQ(session->state(), SessionState::kIdle);

    const auto explained = database->dispatch(*session, "EXPLAIN SELECT * FROM items WHERE id = 2");
    ASSERT_TRUE(explained) << explained.error().to_string();
    ASSERT_EQ(explained->result_sets.size(), 1U);
    const auto& plan = explained->result_sets.front();
    ASSERT_GE(plan.size(), 1U);
    EXPECT_EQ(plan.schema().columns()[0].name, "QUERY PLAN");
    EXPECT_EQ(plan.rows()[0].values()[0].kind(), types::ValueKind::kString);

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(QueryTest, BatchWritesPreserveAlternatingNullAndCharacterPayloads) {
    auto root = test_support::TestDirectory::create("engine-query-batch-payloads");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database);
    auto session = database->new_session();
    ASSERT_TRUE(session);
    expect_statement_ok(
        *database,
        *session,
        "CREATE TABLE batch_rows (id INT, label CHAR(32), replacement CHAR(32))"
    );
    expect_statement_ok(*database, *session, "CREATE UNIQUE INDEX batch_id ON batch_rows (id)");
    expect_statement_ok(
        *database,
        *session,
        "INSERT INTO batch_rows VALUES "
        "(1, 'abcdefghijklmnopqrstuvwxyz012345', NULL), "
        "(2, NULL, 'x'), (3, 'q', 'abcdefghijklmnopqrstuvwxyz012345')"
    );
    auto inserted = database->dispatch(
        *session,
        "INSERT INTO batch_rows SELECT id + 3, label, replacement FROM batch_rows"
    );
    ASSERT_TRUE(inserted) << inserted.error().to_string();
    EXPECT_EQ(inserted->affected_row_count, 3U);
    auto updated = database->dispatch(*session, "UPDATE batch_rows SET label = replacement");
    ASSERT_TRUE(updated) << updated.error().to_string();
    EXPECT_EQ(updated->affected_row_count, 6U);

    const auto expected =
        execute_query(*database, *session, "SELECT replacement FROM batch_rows ORDER BY id");
    const auto actual =
        execute_query(*database, *session, "SELECT label FROM batch_rows ORDER BY id");
    ASSERT_EQ(actual.size(), 6U);
    ASSERT_EQ(expected.size(), actual.size());
    for (std::size_t row = 0U; row < actual.size(); ++row) {
        EXPECT_EQ(actual.rows()[row].values()[0], expected.rows()[row].values()[0]);
    }
    EXPECT_TRUE(actual.rows()[0].values()[0].is_null());
    EXPECT_EQ(actual.rows()[1].values()[0], types::Value::character("x", types::TypeModifier{32}));
    EXPECT_EQ(
        actual.rows()[2].values()[0],
        types::Value::character("abcdefghijklmnopqrstuvwxyz012345", types::TypeModifier{32})
    );

    // Failure after one successful row must roll back the complete batch.
    auto duplicate = database->dispatch(
        *session,
        "INSERT INTO batch_rows VALUES (7, 'temporary', NULL), (1, NULL, NULL)"
    );
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kAlreadyExists);
    EXPECT_EQ(session->state(), SessionState::kIdle);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
    auto reopened = Database::open(test_config(root->path()));
    ASSERT_TRUE(reopened);
    auto reader = reopened->new_session();
    ASSERT_TRUE(reader);
    const auto persisted =
        execute_query(*reopened, *reader, "SELECT label FROM batch_rows ORDER BY id");
    ASSERT_EQ(persisted.size(), actual.size());
    for (std::size_t row = 0U; row < persisted.size(); ++row) {
        EXPECT_EQ(persisted.rows()[row].values()[0], actual.rows()[row].values()[0]);
    }
    ASSERT_TRUE(reader->close());
    ASSERT_TRUE(reopened->close());
}

TEST(QueryTest, PreparedScanUsesCurrentIsolationAndPreservesPreciseSerializablePredicates) {
    auto root = test_support::TestDirectory::create("engine-query-isolation-predicates");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database);
    auto first = database->new_session();
    auto second = database->new_session();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    expect_statement_ok(
        *database,
        *first,
        "CREATE TABLE predicates (id INT, grp INT, payload INT)"
    );
    expect_statement_ok(*database, *first, "CREATE UNIQUE INDEX predicates_id ON predicates (id)");
    expect_statement_ok(*database, *first, "INSERT INTO predicates VALUES (1, 1, 10), (2, 2, 20)");
    const std::array parameter_types{types::kInt64Type};
    auto scan = database->prepare("SELECT payload FROM predicates WHERE grp = $1", parameter_types);
    ASSERT_TRUE(scan);
    const std::array one{types::Value::integer(1)};
    const std::array two{types::Value::integer(2)};
    // Reuse a plan first executed without SSI, then change the session's
    // isolation. The prepared plan must not cache SSI mode.
    auto warmup = database->execute_prepared(*first, *scan, one);
    ASSERT_TRUE(warmup);
    ASSERT_TRUE(first->set_isolation(SessionIsolation::kSerializable));
    ASSERT_TRUE(first->begin());
    ASSERT_TRUE(second->set_isolation(SessionIsolation::kSerializable));
    ASSERT_TRUE(second->begin());
    auto first_read = database->execute_prepared(*first, *scan, one);
    auto second_read = database->execute_prepared(*second, *scan, two);
    ASSERT_TRUE(first_read);
    ASSERT_TRUE(second_read);
    ASSERT_EQ(first_read->row_count, 1U);
    ASSERT_EQ(second_read->row_count, 1U);
    // Disjoint logical predicates must not become whole-table SIREADs.
    expect_statement_ok(*database, *first, "UPDATE predicates SET payload = 11 WHERE id = 1");
    expect_statement_ok(*database, *second, "UPDATE predicates SET payload = 21 WHERE id = 2");
    ASSERT_TRUE(first->commit());
    ASSERT_TRUE(second->commit());

    ASSERT_TRUE(first->begin());
    ASSERT_TRUE(second->begin());
    first_read = database->execute_prepared(*first, *scan, two);
    second_read = database->execute_prepared(*second, *scan, one);
    ASSERT_TRUE(first_read);
    ASSERT_TRUE(second_read);
    expect_statement_ok(*database, *first, "UPDATE predicates SET payload = 12 WHERE id = 1");
    auto conflicting =
        database->dispatch(*second, "UPDATE predicates SET payload = 22 WHERE id = 2");
    ASSERT_FALSE(conflicting);
    EXPECT_EQ(conflicting.error().code(), base::ErrorCode::kSerializationFailure);
    EXPECT_EQ(second->state(), SessionState::kFailedTransaction);
    ASSERT_TRUE(second->rollback());
    ASSERT_TRUE(first->commit());
    ASSERT_TRUE(first->close());
    ASSERT_TRUE(second->close());
    ASSERT_TRUE(database->close());
}

TEST(QueryTest, UpdateOfNonIndexedColumnUsesHotUpdate) {
    auto root = test_support::TestDirectory::create("engine-query-hot-update");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    const auto system_identifier = database->system_identifier();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();

    expect_statement_ok(*database, *session, "CREATE TABLE accounts (id INT, balance INT)");
    expect_statement_ok(
        *database,
        *session,
        "CREATE UNIQUE INDEX accounts_id_idx ON accounts (id)"
    );
    expect_statement_ok(*database, *session, "INSERT INTO accounts VALUES (1, 10)");
    expect_statement_ok(*database, *session, "UPDATE accounts SET balance = 11 WHERE id = 1");

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());

    auto reader = wal::WalReader::open(
        root->path() / "wal",
        wal::WalReaderOptions{
            .system_identifier = system_identifier,
            .timeline = 1U,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    ASSERT_TRUE(reader) << reader.error().to_string();
    std::size_t hot_update_count = 0U;
    for (;;) {
        auto decoded = reader->read_next();
        ASSERT_TRUE(decoded) << decoded.error().to_string();
        if (!decoded->has_value()) {
            break;
        }
        const auto& record = decoded->value().record;
        if (record.resource_manager == wal::WalResourceManager::kHeap
            && record.info == wal::opcode::heap::kHotUpdate) {
            ++hot_update_count;
        }
    }
    EXPECT_EQ(hot_update_count, 1U);
}

TEST(QueryTest, PreparedQueriesAndDmlReuseStorageBackedAdapter) {
    auto root = test_support::TestDirectory::create("engine-query-prepared");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();

    expect_statement_ok(*database, *session, "CREATE TABLE prepared_items (id INT, label CHAR(8))");
    expect_statement_ok(
        *database,
        *session,
        "INSERT INTO prepared_items VALUES (1, 'one'), (2, 'two')"
    );

    const std::array<types::DataType, 1U> query_types{types::kInt64Type};
    auto query = database->prepare("SELECT label FROM prepared_items WHERE id = $1", query_types);
    ASSERT_TRUE(query) << query.error().to_string();
    const std::array<types::Value, 1U> first_parameter{types::Value::integer(2)};
    auto first = database->execute_prepared(*session, *query, first_parameter);
    ASSERT_TRUE(first) << first.error().to_string();
    ASSERT_EQ(first->result_sets.size(), 1U);
    ASSERT_EQ(first->result_sets[0].size(), 1U);
    EXPECT_EQ(
        first->result_sets[0].rows()[0].values()[0],
        types::Value::character("two", types::TypeModifier{8})
    );

    const std::array<types::Value, 1U> second_parameter{types::Value::integer(1)};
    auto second = database->execute_prepared(*session, *query, second_parameter);
    ASSERT_TRUE(second) << second.error().to_string();
    ASSERT_EQ(second->result_sets[0].size(), 1U);
    EXPECT_EQ(
        second->result_sets[0].rows()[0].values()[0],
        types::Value::character("one", types::TypeModifier{8})
    );

    const std::array<types::DataType, 2U> insert_types{
        types::kInt64Type,
        types::DataType{types::kCharTypeId, types::TypeModifier{8}},
    };
    auto insert = database->prepare("INSERT INTO prepared_items VALUES ($1, $2)", insert_types);
    ASSERT_TRUE(insert) << insert.error().to_string();
    const std::array<types::Value, 2U> insert_values{
        types::Value::integer(3),
        types::Value::character("three"),
    };
    auto inserted = database->execute_prepared(*session, *insert, insert_values);
    ASSERT_TRUE(inserted) << inserted.error().to_string();
    EXPECT_EQ(inserted->affected_row_count, 1U);

    const auto count = execute_query(*database, *session, "SELECT COUNT(*) FROM prepared_items");
    ASSERT_EQ(count.size(), 1U);
    EXPECT_EQ(count.rows()[0].values()[0], types::Value::integer(3));

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(QueryTest, CharacterComparisonsAndPreparedIndexBoundsDoNotApplyColumnTypmod) {
    auto root = test_support::TestDirectory::create("engine-query-character-index-bound");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();

    expect_statement_ok(
        *database,
        *session,
        "CREATE TABLE character_items (id INT, short CHAR(2), long CHAR(4))"
    );
    expect_statement_ok(
        *database,
        *session,
        "CREATE INDEX character_items_short_idx ON character_items (short)"
    );
    expect_statement_ok(
        *database,
        *session,
        "INSERT INTO character_items VALUES (1, 'ab', 'abcd'), (2, 'ac', 'ac')"
    );

    const auto comparison = execute_query(
        *database,
        *session,
        "SELECT short = long, long = short FROM character_items WHERE id = 1"
    );
    ASSERT_EQ(comparison.size(), 1U);
    EXPECT_EQ(comparison.rows()[0].values()[0], types::Value::boolean(false));
    EXPECT_EQ(comparison.rows()[0].values()[1], types::Value::boolean(false));

    const auto literal_equality =
        execute_query(*database, *session, "SELECT id FROM character_items WHERE short = 'abc'");
    EXPECT_TRUE(literal_equality.empty());
    const auto literal_range = execute_query(
        *database,
        *session,
        "SELECT id FROM character_items WHERE short < 'aba' ORDER BY id"
    );
    ASSERT_EQ(literal_range.size(), 1U);
    EXPECT_EQ(literal_range.rows()[0].values()[0], types::Value::integer(1));

    const std::array<types::DataType, 1U> parameter_types{types::kCharType};
    auto prepared =
        database->prepare("SELECT id FROM character_items WHERE short = $1", parameter_types);
    ASSERT_TRUE(prepared) << prepared.error().to_string();
    const std::array oversized_parameter{types::Value::character("abc")};
    auto no_match = database->execute_prepared(*session, *prepared, oversized_parameter);
    ASSERT_TRUE(no_match) << no_match.error().to_string();
    ASSERT_EQ(no_match->result_sets.size(), 1U);
    EXPECT_TRUE(no_match->result_sets[0].empty());

    const std::array matching_parameter{types::Value::character("ab")};
    auto match = database->execute_prepared(*session, *prepared, matching_parameter);
    ASSERT_TRUE(match) << match.error().to_string();
    ASSERT_EQ(match->result_sets.size(), 1U);
    ASSERT_EQ(match->result_sets[0].size(), 1U);
    EXPECT_EQ(match->result_sets[0].rows()[0].values()[0], types::Value::integer(1));

    const types::DataType char_two{types::kCharTypeId, types::TypeModifier{2}};
    const std::array<types::DataType, 1U> narrow_parameter_types{char_two};
    auto narrow_prepared = database->prepare(
        "SELECT id FROM character_items WHERE short = $1",
        narrow_parameter_types
    );
    ASSERT_TRUE(narrow_prepared) << narrow_prepared.error().to_string();
    auto truncated_match =
        database->execute_prepared(*session, *narrow_prepared, oversized_parameter);
    ASSERT_TRUE(truncated_match) << truncated_match.error().to_string();
    ASSERT_EQ(truncated_match->result_sets.size(), 1U);
    ASSERT_EQ(truncated_match->result_sets[0].size(), 1U);
    EXPECT_EQ(truncated_match->result_sets[0].rows()[0].values()[0], types::Value::integer(1));

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(QueryTest, QueriesSurviveCheckpointCloseAndReopen) {
    auto root = test_support::TestDirectory::create("engine-query-reopen");
    ASSERT_TRUE(root);
    const auto config = test_config(root->path());
    auto database = Database::create(config);
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();

    expect_statement_ok(*database, *session, "CREATE TABLE durable_items (id INT, label CHAR(8))");
    expect_statement_ok(*database, *session, "INSERT INTO durable_items VALUES (7, 'persisted')");
    const auto checkpoint = database->dispatch(*session, "CHECKPOINT");
    ASSERT_TRUE(checkpoint) << checkpoint.error().to_string();
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());

    auto reopened = Database::open(config);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    auto reopened_session = reopened->new_session();
    ASSERT_TRUE(reopened_session) << reopened_session.error().to_string();
    const auto result =
        execute_query(*reopened, *reopened_session, "SELECT id, label FROM durable_items");
    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result.rows()[0].values()[0], types::Value::integer(7));
    EXPECT_EQ(
        result.rows()[0].values()[1],
        types::Value::character("persiste", types::TypeModifier{8})
    );
    ASSERT_TRUE(reopened_session->close());
    ASSERT_TRUE(reopened->close());
}

TEST(QueryTest, JoinAndSubqueryUseTheSameStoragePorts) {
    auto root = test_support::TestDirectory::create("engine-query-joins");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();

    expect_statement_ok(*database, *session, "CREATE TABLE query_left (id INT, label CHAR(8))");
    expect_statement_ok(*database, *session, "CREATE TABLE query_right (id INT, amount INT)");
    expect_statement_ok(
        *database,
        *session,
        "INSERT INTO query_left VALUES (1, 'one'), (2, 'two'), (3, 'three')"
    );
    expect_statement_ok(
        *database,
        *session,
        "INSERT INTO query_right VALUES (1, 10), (1, 20), (3, 30)"
    );
    expect_statement_ok(
        *database,
        *session,
        "CREATE UNIQUE INDEX query_left_id_idx ON query_left (id)"
    );
    expect_statement_ok(*database, *session, "CREATE INDEX query_right_id_idx ON query_right (id)");

    const auto selective_join = execute_query(
        *database,
        *session,
        "SELECT COUNT(*) FROM query_left l, query_right r "
        "WHERE l.id = 1 AND r.id = l.id"
    );
    ASSERT_EQ(selective_join.size(), 1U);
    EXPECT_EQ(selective_join.rows()[0].values()[0], types::Value::integer(2));

    const auto joined = execute_query(
        *database,
        *session,
        "SELECT l.id, r.amount FROM query_left l JOIN query_right r ON l.id = r.id "
        "ORDER BY l.id, r.amount"
    );
    ASSERT_EQ(joined.size(), 3U);
    EXPECT_EQ(joined.rows()[0].values()[0], types::Value::integer(1));
    EXPECT_EQ(joined.rows()[0].values()[1], types::Value::integer(10));
    EXPECT_EQ(joined.rows()[2].values()[0], types::Value::integer(3));
    EXPECT_EQ(joined.rows()[2].values()[1], types::Value::integer(30));

    const auto subquery = execute_query(
        *database,
        *session,
        "SELECT id FROM query_left WHERE id IN "
        "(SELECT id FROM query_right WHERE amount > 15) ORDER BY id"
    );
    ASSERT_EQ(subquery.size(), 2U);
    EXPECT_EQ(subquery.rows()[0].values()[0], types::Value::integer(1));
    EXPECT_EQ(subquery.rows()[1].values()[0], types::Value::integer(3));

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(QueryTest, BuiltinDispatchPreservesExplicitTransactionRollback) {
    auto root = test_support::TestDirectory::create("engine-query-transaction");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();

    expect_statement_ok(*database, *session, "CREATE TABLE transaction_items (id INT)");
    ASSERT_TRUE(session->begin());
    expect_statement_ok(*database, *session, "INSERT INTO transaction_items VALUES (1)");
    ASSERT_TRUE(session->rollback());

    const auto after_rollback =
        execute_query(*database, *session, "SELECT COUNT(*) FROM transaction_items");
    ASSERT_EQ(after_rollback.size(), 1U);
    EXPECT_EQ(after_rollback.rows()[0].values()[0], types::Value::integer(0));

    ASSERT_TRUE(session->begin());
    expect_statement_ok(*database, *session, "INSERT INTO transaction_items VALUES (2)");
    ASSERT_TRUE(session->commit());
    const auto after_commit =
        execute_query(*database, *session, "SELECT id FROM transaction_items");
    ASSERT_EQ(after_commit.size(), 1U);
    EXPECT_EQ(after_commit.rows()[0].values()[0], types::Value::integer(2));

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

}  // namespace
}  // namespace db::engine
