#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
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

template <typename Result>
void expect_unavailable(const Result& result) {
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inactive(Session& session) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(session.valid());
    EXPECT_EQ(session.state(), SessionState::kIdle);
    EXPECT_EQ(session.isolation(), SessionIsolation::kReadCommitted);
    EXPECT_FALSE(session.command_active());
    expect_unavailable(session.set_isolation(SessionIsolation::kSerializable));
    expect_unavailable(session.begin());
    expect_unavailable(session.commit());
    expect_unavailable(session.rollback());
    expect_unavailable(session.start_command());
    expect_unavailable(session.finish_command(true));
    EXPECT_TRUE(session.close());
}

TEST(SessionTest, ImplicitCommandCommitsAndReturnsToIdle) {
    auto root = test_support::TestDirectory::create("engine-session-implicit");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();

    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    EXPECT_EQ(session->state(), SessionState::kIdle);
    EXPECT_EQ(session->isolation(), SessionIsolation::kReadCommitted);

    ASSERT_TRUE(session->start_command());
    EXPECT_TRUE(session->command_active());
    ASSERT_TRUE(session->finish_command(true));
    EXPECT_EQ(session->state(), SessionState::kIdle);
    EXPECT_FALSE(session->command_active());
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(SessionTest, ExplicitFailureBlocksCommandsUntilRollback) {
    auto root = test_support::TestDirectory::create("engine-session-failed");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();

    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    ASSERT_TRUE(session->begin());
    EXPECT_EQ(session->state(), SessionState::kInTransaction);
    ASSERT_TRUE(session->start_command());
    ASSERT_TRUE(session->finish_command(false));
    EXPECT_EQ(session->state(), SessionState::kFailedTransaction);

    auto rejected = session->start_command();
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kTransactionState);
    auto commit = session->commit();
    ASSERT_FALSE(commit);
    EXPECT_EQ(commit.error().code(), base::ErrorCode::kTransactionState);

    ASSERT_TRUE(session->rollback());
    EXPECT_EQ(session->state(), SessionState::kIdle);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(SessionTest, IsolationDefaultPersistsAndExplicitSnapshotLocksIt) {
    auto root = test_support::TestDirectory::create("engine-session-isolation");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();

    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    ASSERT_TRUE(session->set_isolation(SessionIsolation::kSerializable));
    ASSERT_TRUE(session->start_command());
    ASSERT_TRUE(session->finish_command(true));
    EXPECT_EQ(session->isolation(), SessionIsolation::kSerializable);

    ASSERT_TRUE(session->begin());
    auto changed = session->set_isolation(SessionIsolation::kReadCommitted);
    ASSERT_FALSE(changed);
    EXPECT_EQ(changed.error().code(), base::ErrorCode::kTransactionState);
    ASSERT_TRUE(session->rollback());
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(SessionTest, MoveAssignmentCleansTheDestinationAndAlwaysTransfersOwnership) {
    auto root = test_support::TestDirectory::create("engine-session-move");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();

    auto created = database->new_session();
    ASSERT_TRUE(created) << created.error().to_string();
    Session source = std::move(created).value();
    Session moved{std::move(source)};
    EXPECT_TRUE(moved.valid());
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive(source);

    auto replacement = database->new_session();
    ASSERT_TRUE(replacement) << replacement.error().to_string();
    Session assigned = std::move(replacement).value();
    ASSERT_TRUE(assigned.begin());
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.valid());
    EXPECT_EQ(assigned.state(), SessionState::kIdle);
    // Inspecting the documented inert source is the behavior under test.
    // NOLINTNEXTLINE(bugprone-use-after-move)
    expect_inactive(moved);

    const auto live_session = database->close();
    ASSERT_FALSE(live_session);
    EXPECT_EQ(live_session.error().code(), base::ErrorCode::kUnavailable);
    ASSERT_TRUE(assigned.close());
    ASSERT_TRUE(database->close());
}

TEST(SessionTest, DatabaseCloseRejectsLiveSessionAndDestructionRollsBack) {
    auto root = test_support::TestDirectory::create("engine-session-lifetime");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();

    {
        auto session = database->new_session();
        ASSERT_TRUE(session) << session.error().to_string();
        ASSERT_TRUE(session->begin());
        auto rejected = database->close();
        ASSERT_FALSE(rejected);
        EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnavailable);
    }
    ASSERT_TRUE(database->close());
}

TEST(SessionTest, SessionKeepsDatabaseServicesAliveUntilItIsReleased) {
    auto root = test_support::TestDirectory::create("engine-session-owner-token");
    ASSERT_TRUE(root);
    std::optional<Session> session;
    {
        auto database = Database::create(test_config(root->path()));
        ASSERT_TRUE(database) << database.error().to_string();
        auto created = database->new_session();
        ASSERT_TRUE(created) << created.error().to_string();
        session.emplace(std::move(created).value());
    }

    ASSERT_TRUE(session.has_value());
    ASSERT_TRUE(session->close());
    session.reset();

    auto reopened = Database::open(test_config(root->path()));
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    ASSERT_TRUE(reopened->close());
}

}  // namespace
}  // namespace db::engine
