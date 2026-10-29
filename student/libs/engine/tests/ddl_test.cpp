#include <algorithm>
#include <filesystem>
#include <set>

#include <gtest/gtest.h>

#include "db/engine/database.hpp"
#include "db/engine/session.hpp"
#include "db/test_support/test_directory.hpp"

namespace db::engine {
namespace {

auto test_config(const std::filesystem::path& path) -> DatabaseConfig {
    return DatabaseConfig{
        .data_directory = path,
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };
}

auto relation_files(const std::filesystem::path& root) -> std::set<std::filesystem::path> {
    std::set<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root / "relations"}) {
        if (entry.is_regular_file()) {
            files.insert(entry.path());
        }
    }
    return files;
}

TEST(DdlTest, FailedUniqueBuildRemovesRelationAndAllowsRetryAfterReopen) {
    auto root = test_support::TestDirectory::create("engine-ddl-build-cleanup");
    ASSERT_TRUE(root);
    const auto config = test_config(root->path());
    auto database = Database::create(config);
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session);
    const auto empty_files = relation_files(root->path());
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE items (id INT, tag INT)"));
    ASSERT_TRUE(database->dispatch(*session, "INSERT INTO items VALUES (1, 1), (1, 2), (2, 3)"));
    ASSERT_TRUE(database->dispatch(*session, "CHECKPOINT"));
    const auto table_files = relation_files(root->path());
    EXPECT_FALSE(table_files.empty());

    auto failed = database->dispatch(*session, "CREATE UNIQUE INDEX items_id ON items (id)");
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kAlreadyExists);
    EXPECT_TRUE(
        std::any_of(
            failed.error().contexts().begin(),
            failed.error().contexts().end(),
            [](const base::ErrorContext& context) {
                return context.component == "engine.utility"
                       && context.operation == "bulk build index relation";
            }
        )
    );
    EXPECT_EQ(session->state(), SessionState::kIdle);
    ASSERT_TRUE(database->dispatch(*session, "CHECKPOINT"));
    EXPECT_EQ(relation_files(root->path()), table_files);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());

    auto reopened = Database::open(config);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    auto next_session = reopened->new_session();
    ASSERT_TRUE(next_session);
    auto preserved = reopened->dispatch(*next_session, "SELECT tag FROM items ORDER BY tag");
    ASSERT_TRUE(preserved);
    ASSERT_EQ(preserved->result_sets.size(), 1U);
    EXPECT_EQ(preserved->result_sets.front().size(), 3U);

    ASSERT_TRUE(reopened->dispatch(*next_session, "DELETE FROM items WHERE tag = 2"));
    auto retried = reopened->dispatch(*next_session, "CREATE UNIQUE INDEX items_id ON items (id)");
    ASSERT_TRUE(retried) << retried.error().to_string();
    auto duplicate = reopened->dispatch(*next_session, "INSERT INTO items VALUES (1, 4)");
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kAlreadyExists);
    ASSERT_TRUE(reopened->dispatch(*next_session, "DROP TABLE items"));
    ASSERT_TRUE(reopened->dispatch(*next_session, "CHECKPOINT"));
    EXPECT_EQ(relation_files(root->path()), empty_files);
    ASSERT_TRUE(next_session->close());
    ASSERT_TRUE(reopened->close());
}

TEST(DdlTest, IndexNoOpSkipsBuildAndBuildFailureRequiresExplicitRollback) {
    auto root = test_support::TestDirectory::create("engine-ddl-failed-block");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database);
    auto session = database->new_session();
    ASSERT_TRUE(session);
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE items (id INT)"));
    ASSERT_TRUE(database->dispatch(*session, "INSERT INTO items VALUES (1), (1)"));
    ASSERT_TRUE(database->dispatch(*session, "CREATE INDEX items_id ON items (id)"));
    ASSERT_TRUE(database->dispatch(*session, "CHECKPOINT"));
    const auto files = relation_files(root->path());
    auto no_op =
        database->dispatch(*session, "CREATE UNIQUE INDEX IF NOT EXISTS items_id ON items (id)");
    ASSERT_TRUE(no_op) << no_op.error().to_string();
    EXPECT_EQ(relation_files(root->path()), files);

    ASSERT_TRUE(session->begin());
    auto failed = database->dispatch(*session, "CREATE UNIQUE INDEX unique_id ON items (id)");
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kAlreadyExists);
    EXPECT_EQ(session->state(), SessionState::kFailedTransaction);
    auto blocked = database->dispatch(*session, "DROP INDEX items_id");
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code(), base::ErrorCode::kTransactionState);
    ASSERT_TRUE(session->rollback());
    EXPECT_EQ(session->state(), SessionState::kIdle);
    ASSERT_TRUE(database->dispatch(*session, "CHECKPOINT"));
    EXPECT_EQ(relation_files(root->path()), files);

    auto retry = database->dispatch(*session, "CREATE INDEX unique_id ON items (id)");
    ASSERT_TRUE(retry) << retry.error().to_string();
    ASSERT_TRUE(database->dispatch(*session, "DROP INDEX unique_id"));
    ASSERT_TRUE(database->dispatch(*session, "DROP TABLE items"));
    ASSERT_TRUE(database->dispatch(*session, "CHECKPOINT"));
    EXPECT_TRUE(relation_files(root->path()).empty());
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

}  // namespace
}  // namespace db::engine
