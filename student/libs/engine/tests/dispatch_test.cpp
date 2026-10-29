#include <cstddef>
#include <filesystem>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/catalog/store.hpp"
#include "db/engine/database.hpp"
#include "db/engine/dispatch.hpp"
#include "db/engine/session.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/wal/control_store.hpp"

namespace db::engine {
namespace {

auto test_config(const std::filesystem::path& path) -> DatabaseConfig {
    return DatabaseConfig{
        .data_directory = path,
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };
}

class RecordingExecution final : public DispatchExecutionPort {
public:
    auto execute(const DispatchPlan& plan, const std::span<const types::Value> parameter_values)
        -> base::Result<DispatchExecutionResult> override {
        ++calls_;
        last_kind_ = plan.kind();
        last_generation_ = plan.catalog_generation();
        last_parameters_.assign(parameter_values.begin(), parameter_values.end());
        return DispatchExecutionResult{.row_count = 1U, .affected_row_count = 0U};
    }

    auto calls() const noexcept -> std::size_t {
        return calls_;
    }

    auto last_kind() const noexcept -> DispatchStatementKind {
        return last_kind_;
    }

    auto last_generation() const noexcept -> std::uint64_t {
        return last_generation_;
    }

    auto last_parameter_count() const noexcept -> std::size_t {
        return last_parameters_.size();
    }

    auto last_parameters() const noexcept -> std::span<const types::Value> {
        return last_parameters_;
    }

private:
    std::size_t calls_{0U};
    DispatchStatementKind last_kind_{DispatchStatementKind::kScript};
    std::uint64_t last_generation_{0U};
    std::vector<types::Value> last_parameters_;
};

class ResultExecution final : public DispatchExecutionPort {
public:
    auto execute(const DispatchPlan& plan, const std::span<const types::Value> parameter_values)
        -> base::Result<DispatchExecutionResult> override {
        static_cast<void>(plan);
        static_cast<void>(parameter_values);
        auto schema = ResultSchema::create({ResultColumn{
            .name = "answer",
            .data_type = types::kInt64Type,
        }});
        if (!schema) {
            return base::fail(std::move(schema).error());
        }
        auto result_set = ResultSet::create(
            std::move(schema).value(),
            std::vector<std::vector<types::Value>>{{types::Value::integer(42)}}
        );
        if (!result_set) {
            return base::fail(std::move(result_set).error());
        }
        return DispatchExecutionResult{
            .row_count = result_set->size(),
            .affected_row_count = 0U,
            .result_set = std::move(result_set).value(),
        };
    }
};

class ThrowingExecution final : public DispatchExecutionPort {
public:
    explicit ThrowingExecution(const bool allocation_failure) noexcept
        : allocation_failure_{allocation_failure} {}

    auto execute(const DispatchPlan& plan, const std::span<const types::Value> parameter_values)
        -> base::Result<DispatchExecutionResult> override {
        static_cast<void>(plan);
        static_cast<void>(parameter_values);
        if (allocation_failure_) {
            throw std::bad_alloc{};
        }
        throw std::runtime_error{"execution adapter failed"};
    }

private:
    bool allocation_failure_;
};

TEST(DispatchTest, ParseBindPlanAndExecuteUseOneCatalogGeneration) {
    auto root = test_support::TestDirectory::create("engine-dispatch-pipeline");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;

    const auto result = database->dispatch(*session, "SELECT 7", execution, DispatchParameters{});
    ASSERT_TRUE(result) << result.error().to_string();
    EXPECT_EQ(result->statement_count, 1U);
    EXPECT_EQ(result->row_count, 1U);
    EXPECT_EQ(result->kind, DispatchStatementKind::kQuery);
    EXPECT_EQ(execution.calls(), 1U);
    EXPECT_EQ(execution.last_kind(), DispatchStatementKind::kQuery);
    EXPECT_NE(execution.last_generation(), 0U);
    EXPECT_EQ(execution.last_parameter_count(), 0U);
    EXPECT_EQ(session->state(), SessionState::kIdle);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(DispatchTest, PreparesDirectParametersBeforeExecution) {
    auto root = test_support::TestDirectory::create("engine-dispatch-parameters");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;
    const types::DataType char_four{types::kCharTypeId, types::TypeModifier{4}};
    const std::vector parameter_types{char_four};
    const std::vector parameter_values{types::Value::character("abcdef")};

    auto result = database->dispatch(
        *session,
        "SELECT $1",
        execution,
        DispatchParameters{.types = parameter_types, .values = parameter_values}
    );
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(execution.last_parameters().size(), 1U);
    EXPECT_EQ(
        execution.last_parameters()[0],
        types::Value::character("abcd", char_four.modifier())
    );
    EXPECT_EQ(parameter_values[0], types::Value::character("abcdef"));

    const std::vector wrong_values{types::Value::integer(7)};
    auto rejected = database->dispatch(
        *session,
        "SELECT $1",
        execution,
        DispatchParameters{.types = parameter_types, .values = wrong_values}
    );
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(execution.calls(), 1U);
    EXPECT_EQ(session->state(), SessionState::kIdle);

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(DispatchTest, MovesOptionalResultSetPayloadAcrossExecutionBoundary) {
    auto root = test_support::TestDirectory::create("engine-dispatch-result-payload");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    ResultExecution execution;

    auto result = database->dispatch(*session, "SELECT 7", execution);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->result_sets.size(), 1U);
    const auto& result_set = result->result_sets.front();
    ASSERT_EQ(result_set.schema().size(), 1U);
    EXPECT_EQ(result_set.schema().columns()[0].name, "answer");
    ASSERT_EQ(result_set.size(), 1U);
    ASSERT_TRUE(result_set.at(0U));
    ASSERT_TRUE(result_set.at(0U)->get().at(0U));
    EXPECT_EQ(result_set.at(0U)->get().at(0U)->get(), types::Value::integer(42));

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(DispatchTest, BindFailureAbortsImplicitCommandAndLeavesSessionReusable) {
    auto root = test_support::TestDirectory::create("engine-dispatch-failure");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;

    auto rejected = database->dispatch(*session, "SELECT missing_column", execution);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kBind);
    EXPECT_EQ(session->state(), SessionState::kIdle);
    EXPECT_EQ(execution.calls(), 0U);

    auto valid = database->dispatch(*session, "SELECT 9", execution);
    ASSERT_TRUE(valid) << valid.error().to_string();
    EXPECT_EQ(execution.calls(), 1U);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(DispatchTest, ConvertsExecutionPortExceptionsAndLeavesSessionReusable) {
    auto root = test_support::TestDirectory::create("engine-dispatch-port-exception");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();

    ThrowingExecution standard_exception{false};
    auto internal = database->dispatch(*session, "SELECT 1", standard_exception);
    ASSERT_FALSE(internal);
    EXPECT_EQ(internal.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(internal.error().message().find("execution adapter failed"), std::string_view::npos);
    EXPECT_EQ(session->state(), SessionState::kIdle);

    ThrowingExecution allocation_exception{true};
    auto exhausted = database->dispatch(*session, "SELECT 2", allocation_exception);
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_EQ(session->state(), SessionState::kIdle);

    RecordingExecution recovered_execution;
    auto recovered = database->dispatch(*session, "SELECT 3", recovered_execution);
    ASSERT_TRUE(recovered) << recovered.error().to_string();
    EXPECT_EQ(recovered_execution.calls(), 1U);

    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(DispatchTest, ExplicitFailedBlockRejectsLaterDispatchUntilRollback) {
    auto root = test_support::TestDirectory::create("engine-dispatch-explicit-failure");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    ASSERT_TRUE(session->begin());
    RecordingExecution execution;

    auto rejected = database->dispatch(*session, "SELECT missing_column", execution);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kBind);
    EXPECT_EQ(session->state(), SessionState::kFailedTransaction);

    auto blocked = database->dispatch(*session, "SELECT 9", execution);
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code(), base::ErrorCode::kTransactionState);
    ASSERT_TRUE(session->rollback());
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(DispatchTest, RejectsSessionOwnedByAnotherDatabase) {
    auto first_root = test_support::TestDirectory::create("engine-dispatch-first-owner");
    ASSERT_TRUE(first_root);
    auto second_root = test_support::TestDirectory::create("engine-dispatch-second-owner");
    ASSERT_TRUE(second_root);
    auto first = Database::create(test_config(first_root->path()));
    ASSERT_TRUE(first) << first.error().to_string();
    auto second = Database::create(test_config(second_root->path()));
    ASSERT_TRUE(second) << second.error().to_string();
    auto foreign_session = second->new_session();
    ASSERT_TRUE(foreign_session) << foreign_session.error().to_string();
    RecordingExecution execution;

    auto rejected = first->dispatch(*foreign_session, "SELECT 1", execution);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(execution.calls(), 0U);
    ASSERT_TRUE(foreign_session->close());
    ASSERT_TRUE(first->close());
    ASSERT_TRUE(second->close());
}

TEST(DispatchTest, UtilityDdlOwnsCatalogAndPhysicalRelationLifecycle) {
    auto root = test_support::TestDirectory::create("engine-dispatch-utility-ddl");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;

    auto created =
        database->dispatch(*session, "CREATE TABLE items (id INT, score FLOAT)", execution);
    ASSERT_TRUE(created) << created.error().to_string();
    EXPECT_EQ(created->kind, DispatchStatementKind::kUtility);
    EXPECT_EQ(execution.calls(), 0U);

    auto duplicate =
        database->dispatch(*session, "CREATE TABLE items (id INT, score FLOAT)", execution);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kAlreadyExists);

    auto repeated = database->dispatch(
        *session,
        "CREATE TABLE IF NOT EXISTS items (id INT, score FLOAT)",
        execution
    );
    ASSERT_TRUE(repeated) << repeated.error().to_string();

    auto index =
        database->dispatch(*session, "CREATE UNIQUE INDEX items_id_idx ON items (id)", execution);
    ASSERT_TRUE(index) << index.error().to_string();
    auto index_again = database->dispatch(
        *session,
        "CREATE UNIQUE INDEX IF NOT EXISTS items_id_idx ON items (id)",
        execution
    );
    ASSERT_TRUE(index_again) << index_again.error().to_string();

    auto dropped_index = database->dispatch(*session, "DROP INDEX items_id_idx", execution);
    ASSERT_TRUE(dropped_index) << dropped_index.error().to_string();
    auto missing_index =
        database->dispatch(*session, "DROP INDEX IF EXISTS items_id_idx", execution);
    ASSERT_TRUE(missing_index) << missing_index.error().to_string();

    auto dropped_table = database->dispatch(*session, "DROP TABLE items", execution);
    ASSERT_TRUE(dropped_table) << dropped_table.error().to_string();
    auto missing_table = database->dispatch(*session, "DROP TABLE IF EXISTS items", execution);
    ASSERT_TRUE(missing_table) << missing_table.error().to_string();

    EXPECT_EQ(execution.calls(), 0U);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(DispatchTest, UtilityDdlReopensRelationBeforeBuildingIndex) {
    auto root = test_support::TestDirectory::create("engine-dispatch-utility-reopen");
    ASSERT_TRUE(root);
    const auto config = test_config(root->path());
    auto database = Database::create(config);
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE items (id INT)", execution));
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());

    auto reopened = Database::open(config);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    auto reopened_session = reopened->new_session();
    ASSERT_TRUE(reopened_session) << reopened_session.error().to_string();
    auto index =
        reopened->dispatch(*reopened_session, "CREATE INDEX items_id_idx ON items (id)", execution);
    ASSERT_TRUE(index) << index.error().to_string();
    auto dropped = reopened->dispatch(*reopened_session, "DROP TABLE items", execution);
    ASSERT_TRUE(dropped) << dropped.error().to_string();
    ASSERT_TRUE(reopened_session->close());
    ASSERT_TRUE(reopened->close());
}

TEST(DispatchTest, UtilityAnalyzePublishesDeterministicStatistics) {
    auto root = test_support::TestDirectory::create("engine-dispatch-analyze");
    ASSERT_TRUE(root);
    const auto config = test_config(root->path());
    auto database = Database::create(config);
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;

    ASSERT_TRUE(
        database->dispatch(*session, "CREATE TABLE items (id INT, score FLOAT)", execution)
    );
    auto analyzed = database->dispatch(*session, "ANALYZE items", execution);
    ASSERT_TRUE(analyzed) << analyzed.error().to_string();
    EXPECT_EQ(analyzed->kind, DispatchStatementKind::kUtility);
    EXPECT_EQ(analyzed->row_count, 0U);
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());

    auto catalog = catalog::CatalogStore::open(
        root->join("catalog"),
        catalog::CatalogStoreOptions{
            .decode = catalog::CatalogDecodeOptions{
                .row_size_limit =
                    catalog::RowSizeLimit{
                        .maximum_encoded_size = config.maximum_encoded_row_size,
                    },
                .maximum_image_size = config.maximum_catalog_image_size,
            },
        }
    );
    ASSERT_TRUE(catalog) << catalog.error().to_string();
    auto table = catalog->lookup_table("items");
    ASSERT_TRUE(table) << table.error().to_string();
    ASSERT_TRUE(table->has_value());
    auto statistics = catalog->table_statistics(table->value().oid());
    ASSERT_TRUE(statistics) << statistics.error().to_string();
    ASSERT_TRUE(statistics->has_value());
    EXPECT_EQ(statistics->value().live_rows, 0U);
    EXPECT_EQ(statistics->value().sample_rows, 0U);
    EXPECT_EQ(statistics->value().dead_rows, 0U);
    EXPECT_NE(statistics->value().analyzed_xid, base::kInvalidXid);
}

TEST(DispatchTest, UtilityVacuumAndShowTablesUseMaintenanceOrdering) {
    auto root = test_support::TestDirectory::create("engine-dispatch-maintenance");
    ASSERT_TRUE(root);
    auto database = Database::create(test_config(root->path()));
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;

    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE zeta (id INT)", execution));
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE alpha (id INT)", execution));
    auto shown = database->dispatch(*session, "SHOW TABLES", execution);
    ASSERT_TRUE(shown) << shown.error().to_string();
    EXPECT_EQ(shown->row_count, 2U);
    ASSERT_EQ(shown->result_sets.size(), 1U);
    const auto& table_rows = shown->result_sets.front();
    ASSERT_EQ(table_rows.schema().size(), 1U);
    EXPECT_EQ(table_rows.schema().columns()[0].name, "table_name");
    ASSERT_EQ(table_rows.size(), 2U);
    ASSERT_TRUE(table_rows.at(0U));
    ASSERT_TRUE(table_rows.at(1U));
    ASSERT_TRUE(table_rows.at(0U)->get().at("table_name"));
    ASSERT_TRUE(table_rows.at(1U)->get().at("table_name"));
    EXPECT_EQ(table_rows.at(0U)->get().at("table_name")->get(), types::Value::character("alpha"));
    EXPECT_EQ(table_rows.at(1U)->get().at("table_name")->get(), types::Value::character("zeta"));
    auto all_vacuumed = database->dispatch(*session, "VACUUM", execution);
    ASSERT_TRUE(all_vacuumed) << all_vacuumed.error().to_string();
    auto one_vacuumed = database->dispatch(*session, "VACUUM alpha", execution);
    ASSERT_TRUE(one_vacuumed) << one_vacuumed.error().to_string();
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());
}

TEST(DispatchTest, UtilityCheckpointPublishesDurableControlHorizon) {
    auto root = test_support::TestDirectory::create("engine-dispatch-checkpoint");
    ASSERT_TRUE(root);
    const auto config = test_config(root->path());
    auto database = Database::create(config);
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();
    RecordingExecution execution;
    ASSERT_TRUE(database->dispatch(*session, "CREATE TABLE items (id INT)", execution));
    auto checkpoint = database->dispatch(*session, "CHECKPOINT", execution);
    ASSERT_TRUE(checkpoint) << checkpoint.error().to_string();
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());

    auto control = wal::WalControlStore::open(root->join("control"));
    ASSERT_TRUE(control) << control.error().to_string();
    EXPECT_NE(control->current().checkpoint_lsn, base::kInvalidLsn);
    EXPECT_NE(control->current().redo_lsn, base::kInvalidLsn);
    EXPECT_EQ(control->current().state, wal::DatabaseState::kShutdown);
}

}  // namespace
}  // namespace db::engine
