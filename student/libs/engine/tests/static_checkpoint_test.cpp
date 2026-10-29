#include <chrono>
#include <filesystem>
#include <future>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/engine/database.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/control_store.hpp"
#include "db/wal/reader.hpp"

namespace db::engine {
namespace {
using namespace std::chrono_literals;

class StaticCheckpointTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto root = test_support::TestDirectory::create("engine-static-checkpoint");
        ASSERT_TRUE(root);
        root_.emplace(std::move(*root));
        auto database = Database::create(
            DatabaseConfig{
                .data_directory = root_->path(),
                .buffer_pool_pages = 8U,
                .wal_ring_pages = 8U,
            }
        );
        ASSERT_TRUE(database) << database.error().to_string();
        database_.emplace(std::move(*database));
        auto session = database_->new_session();
        ASSERT_TRUE(session);
        session_.emplace(std::move(*session));
    }

    void TearDown() override {
        if (session_) {
            EXPECT_TRUE(session_->close());
        }
        if (database_) {
            EXPECT_TRUE(database_->close());
        }
    }

    auto control() -> wal::WalControlImage {
        auto store = wal::WalControlStore::open(root_->join("control"));
        EXPECT_TRUE(store) << store.error().to_string();
        return store->current();
    }

    template <typename Result>
    void unavailable(const Result& result) {
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code(), base::ErrorCode::kUnavailable)
            << result.error().to_string();
    }

    void ok(Session& session, const std::string_view sql) {
        auto result = database_->dispatch(session, sql);
        ASSERT_TRUE(result) << sql << ": " << result.error().to_string();
    }

    auto observe_closed_barrier(Session& probe) -> bool {
        // The already-open transaction prevents the checkpoint from completing
        // during this observation, so observing kUnavailable is a handshake.
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < deadline) {
            auto started = probe.begin();
            if (!started) {
                return started.error().code() == base::ErrorCode::kUnavailable;
            }
            if (!probe.rollback()) {
                return false;
            }
            std::this_thread::yield();
        }
        return false;
    }

    std::optional<test_support::TestDirectory> root_;
    std::optional<Database> database_;
    std::optional<Session> session_;
};

TEST_F(StaticCheckpointTest, PublishesDurableGenerationFromIdleSqlAndApi) {
    auto idle = database_->new_session();
    ASSERT_TRUE(idle);
    ok(*session_, "CREATE TABLE ck (id INT)");
    ok(*session_, "CREATE INDEX ck_id ON ck (id)");
    ok(*session_, "INSERT INTO ck VALUES (1)");
    const auto before = control();
    ok(*session_, "/* lead */ CrEaTe /* nested /* comment */ ok */ STATIC_CHECKPOINT; -- tail");
    const auto after = control();
    EXPECT_EQ(after.generation, before.generation + 1U);
    EXPECT_NE(after.redo_lsn, base::kInvalidLsn);
    EXPECT_GT(after.checkpoint_lsn.value(), after.redo_lsn.value());
    EXPECT_EQ(session_->state(), SessionState::kIdle);
    EXPECT_FALSE(session_->command_active());
    auto reader = wal::WalReader::open(
        root_->join("wal"),
        wal::WalReaderOptions{
            .system_identifier = database_->system_identifier(),
            .timeline = 1U,
            .retention_lsn = after.redo_lsn,
        }
    );
    ASSERT_TRUE(reader) << reader.error().to_string();
    bool found_checkpoint = false;
    while (true) {
        auto record = reader->read_next();
        ASSERT_TRUE(record) << record.error().to_string();
        if (!record->has_value()) {
            break;
        }
        if ((*record)->start_lsn == after.checkpoint_lsn) {
            found_checkpoint = true;
        }
    }
    EXPECT_TRUE(found_checkpoint);
    ASSERT_TRUE(
        database_->static_checkpoint(*session_, StaticCheckpointOptions{.drain_timeout = 0ms})
    );
    EXPECT_EQ(control().generation, after.generation + 1U);
}

TEST_F(StaticCheckpointTest, RejectsInitiatorTransactionAndMixedScriptsWithoutPartialExecution) {
    ASSERT_TRUE(session_->begin());
    auto explicit_result = database_->static_checkpoint(*session_);
    ASSERT_FALSE(explicit_result);
    EXPECT_EQ(explicit_result.error().code(), base::ErrorCode::kTransactionState);
    EXPECT_EQ(session_->state(), SessionState::kInTransaction);
    ASSERT_FALSE(database_->dispatch(*session_, "SELECT missing"));
    EXPECT_EQ(session_->state(), SessionState::kFailedTransaction);
    auto failed_result = database_->dispatch(*session_, "CREATE STATIC_CHECKPOINT");
    ASSERT_FALSE(failed_result);
    EXPECT_EQ(failed_result.error().code(), base::ErrorCode::kTransactionState);
    ASSERT_TRUE(session_->rollback());
    auto mixed = database_->dispatch(
        *session_,
        "CREATE TABLE should_not_exist (id INT); CREATE STATIC_CHECKPOINT;"
    );
    ASSERT_FALSE(mixed);
    EXPECT_EQ(mixed.error().code(), base::ErrorCode::kTransactionState);
    EXPECT_FALSE(database_->dispatch(*session_, "SELECT * FROM should_not_exist"));
    EXPECT_FALSE(database_->prepare("CREATE STATIC_CHECKPOINT"));
    ok(*session_, "SELECT 'CREATE STATIC_CHECKPOINT' AS text");
    ok(*session_, "SELECT 1 /* CREATE STATIC_CHECKPOINT */");
    EXPECT_FALSE(database_->dispatch(*session_, "CREATE STATIC_CHECKPOINT_SUFFIX"));
    EXPECT_FALSE(database_->dispatch(*session_, "CREATE STATIC_CHECKPOINT garbage"));
    ASSERT_TRUE(database_->static_checkpoint(*session_));
}

TEST_F(StaticCheckpointTest, DrainsOldTransactionsAndRejectsEveryNewCommandEntry) {
    ok(*session_, "CREATE TABLE draining (id INT)");
    auto writer = database_->new_session();
    auto probe = database_->new_session();
    ASSERT_TRUE(writer);
    ASSERT_TRUE(probe);
    auto prepared = database_->prepare("SELECT 1");
    ASSERT_TRUE(prepared);
    ASSERT_TRUE(writer->begin());
    ok(*writer, "INSERT INTO draining VALUES (1)");
    auto checkpoint = std::async(std::launch::async, [this] {
        return database_->static_checkpoint(
            *session_,
            StaticCheckpointOptions{.drain_timeout = 5s}
        );
    });
    const bool blocked = observe_closed_barrier(*probe);
    EXPECT_TRUE(blocked);
    if (blocked) {
        EXPECT_EQ(checkpoint.wait_for(0ms), std::future_status::timeout);
        unavailable(probe->start_command());
        unavailable(database_->dispatch(*probe, "SELECT 1"));
        unavailable(database_->execute_prepared(*probe, *prepared));
        std::istringstream input{"2\n"};
        unavailable(database_->copy_from("COPY draining FROM STDIN", input, *probe));
        std::istringstream load{"2\n"};
        unavailable(database_->load_into_empty_from("COPY draining FROM STDIN", load, 2U, *probe));
        bool pulled = false;
        unavailable(database_->copy_from_rows(
            "COPY draining FROM STDIN",
            [&pulled]() -> base::Result<std::optional<std::vector<types::Value>>> {
                pulled = true;
                return std::optional<std::vector<types::Value>>{};
            },
            *probe
        ));
        EXPECT_FALSE(pulled);
        unavailable(
            database_->static_checkpoint(*probe, StaticCheckpointOptions{.drain_timeout = 0ms})
        );
        ok(*writer, "UPDATE draining SET id = 3 WHERE id = 1");
    }
    ASSERT_TRUE(writer->commit());
    auto completed = checkpoint.get();
    ASSERT_TRUE(completed) << completed.error().to_string();
    ok(*probe, "SELECT id FROM draining WHERE id = 3");
    ASSERT_TRUE(probe->begin());
    ASSERT_TRUE(probe->rollback());
}

TEST_F(StaticCheckpointTest, TimeoutReleasesBarrierWithoutPublishing) {
    auto active = database_->new_session();
    auto probe = database_->new_session();
    ASSERT_TRUE(active);
    ASSERT_TRUE(probe);
    ASSERT_TRUE(active->begin());
    const auto before = control();
    unavailable(
        database_->static_checkpoint(*session_, StaticCheckpointOptions{.drain_timeout = 0ms})
    );
    EXPECT_EQ(control(), before);
    ASSERT_TRUE(probe->begin());
    ASSERT_TRUE(probe->rollback());
    ASSERT_TRUE(active->rollback());
    ASSERT_TRUE(
        database_->static_checkpoint(*session_, StaticCheckpointOptions{.drain_timeout = 0ms})
    );
    EXPECT_EQ(control().generation, before.generation + 1U);
}

TEST_F(StaticCheckpointTest, ControlStagingFailureRetainsOldGenerationAndReleasesBarrier) {
    ok(*session_, "CREATE TABLE persist (id INT)");
    ok(*session_, "INSERT INTO persist VALUES (1)");
    ASSERT_TRUE(database_->static_checkpoint(*session_));
    const auto before = control();
    const auto staged = root_->join("control/control.next");
    const auto backup = root_->join("control/saved.next");
    std::filesystem::rename(staged, backup);
    ASSERT_TRUE(std::filesystem::create_directory(staged));
    auto failed = database_->static_checkpoint(*session_);
    EXPECT_FALSE(failed);
    // Restore the injected obstruction before inspecting the disk or shutdown.
    EXPECT_TRUE(std::filesystem::remove(staged));
    std::filesystem::rename(backup, staged);
    EXPECT_EQ(control(), before);
    ASSERT_TRUE(session_->begin());
    ASSERT_TRUE(session_->rollback());
    ASSERT_TRUE(database_->static_checkpoint(*session_));
    EXPECT_EQ(control().generation, before.generation + 1U);
}

}  // namespace
}  // namespace db::engine
