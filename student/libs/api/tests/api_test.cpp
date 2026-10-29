#include <array>
#include <filesystem>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "db/api/api.hpp"
#include "db/test_support/test_directory.hpp"

namespace db::api {
namespace {

void expect_inactive(Database& database) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(database.is_open());
    EXPECT_EQ(database.state(), DatabaseState::kShutdown);
    EXPECT_TRUE(database.data_directory().empty());
    EXPECT_EQ(database.system_identifier(), 0U);
    EXPECT_TRUE(database.close());
}

void expect_inactive(Session& session) {
    // The caller deliberately supplies the documented moved-from source.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(session.valid());
    EXPECT_EQ(session.state(), SessionState::kIdle);
    EXPECT_EQ(session.isolation(), SessionIsolation::kReadCommitted);
    EXPECT_FALSE(session.command_active());
    const auto started = session.begin();
    ASSERT_FALSE(started);
    EXPECT_EQ(started.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_TRUE(session.close());
}

TEST(ApiTest, ExposesValueAndResultContractsWithoutPhysicalTypes) {
    const auto value = Value::integer(7);
    EXPECT_EQ(value.data_type(), DataType{base::TypeId{2}});

    auto schema = ResultSchema::create({
        ResultColumn{.name = "answer", .data_type = value.data_type()},
    });
    ASSERT_TRUE(schema) << schema.error().to_string();
    auto result = ResultSet::create(std::move(schema).value(), {{value}});
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_TRUE(result->at(0U));
    EXPECT_EQ(result->at(0U)->get().at(0U)->get(), value);
    EXPECT_EQ(
        database_error_kind_name(DatabaseErrorKind::kInvalidArgument),
        std::string_view{"invalid_argument"}
    );
}

TEST(ApiTest, DurableDatabaseAndPreparedQueriesUseTheStableFacade) {
    auto root = test_support::TestDirectory::create("api-durable-query");
    ASSERT_TRUE(root);
    DatabaseConfig config{
        .data_directory = root->path(),
        .buffer_pool_pages = 8U,
        .wal_ring_pages = 8U,
    };
    auto database = Database::create(config);
    ASSERT_TRUE(database) << database.error().to_string();
    auto session = database->new_session();
    ASSERT_TRUE(session) << session.error().to_string();

    auto created = database->dispatch(*session, "CREATE TABLE api_items (id INT, label CHAR(8))");
    ASSERT_TRUE(created) << created.error().to_string();
    auto inserted = database->dispatch(*session, "INSERT INTO api_items VALUES (1, 'one')");
    ASSERT_TRUE(inserted) << inserted.error().to_string();

    const std::array<DataType, 1U> parameter_types{types::kInt64Type};
    auto prepared = database->prepare("SELECT label FROM api_items WHERE id = $1", parameter_types);
    ASSERT_TRUE(prepared) << prepared.error().to_string();
    EXPECT_TRUE(prepared->valid());
    EXPECT_EQ(prepared->kind(), DispatchStatementKind::kQuery);
    EXPECT_EQ(prepared->parameter_count(), 1U);
    ASSERT_EQ(prepared->parameter_types().size(), 1U);
    EXPECT_EQ(prepared->parameter_types()[0], types::kInt64Type);
    ASSERT_EQ(prepared->result_columns().size(), 1U);
    EXPECT_EQ(prepared->result_columns()[0].name, "label");
    const types::DataType expected_result_type{
        types::kCharTypeId,
        types::TypeModifier{8},
    };
    EXPECT_EQ(prepared->result_columns()[0].data_type, expected_result_type);
    const std::array<Value, 1U> parameter_values{Value::integer(1)};
    auto result = database->execute_prepared(*session, *prepared, parameter_values);
    ASSERT_TRUE(result) << result.error().to_string();
    ASSERT_EQ(result->result_sets.size(), 1U);
    ASSERT_EQ(result->result_sets.front().size(), 1U);
    EXPECT_EQ(
        result->result_sets.front().rows()[0].values()[0],
        Value::character("one", types::TypeModifier{8})
    );

    auto checkpoint = database->dispatch(*session, "CHECKPOINT");
    ASSERT_TRUE(checkpoint) << checkpoint.error().to_string();
    ASSERT_TRUE(session->close());
    ASSERT_TRUE(database->close());

    auto reopened = Database::open(config);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    auto reopened_session = reopened->new_session();
    ASSERT_TRUE(reopened_session) << reopened_session.error().to_string();
    auto reopened_result = reopened->dispatch(*reopened_session, "SELECT id FROM api_items");
    ASSERT_TRUE(reopened_result) << reopened_result.error().to_string();
    ASSERT_EQ(reopened_result->result_sets.front().size(), 1U);
    EXPECT_EQ(reopened_result->result_sets.front().rows()[0].values()[0], Value::integer(1));
    ASSERT_TRUE(reopened_session->close());
    ASSERT_TRUE(reopened->close());
}

TEST(ApiTest, OpenOrCreateOwnsDatabaseSelectionBehindTheStableFacade) {
    auto root = test_support::TestDirectory::create("api-open-or-create");
    ASSERT_TRUE(root) << root.error().to_string();
    DatabaseConfig config{
        .data_directory = root->path(),
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };

    auto initialized = Database::open_or_create(config);
    ASSERT_TRUE(initialized) << initialized.error().to_string();
    const auto system_identifier = initialized->system_identifier();
    ASSERT_TRUE(initialized->close());

    auto reopened = Database::open_or_create(root->path(), config);
    ASSERT_TRUE(reopened) << reopened.error().to_string();
    EXPECT_EQ(reopened->system_identifier(), system_identifier);
    ASSERT_TRUE(reopened->close());
}

TEST(ApiTest, DatabaseMoveConstructionLeavesOneExplicitCloseOwner) {
    auto root = test_support::TestDirectory::create("api-database-move");
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
    ASSERT_TRUE(moved.close());
}

TEST(ApiTest, SessionMoveAssignmentCleansTheDestinationAndExposesAnInertSource) {
    auto root = test_support::TestDirectory::create("api-session-move");
    ASSERT_TRUE(root);
    DatabaseConfig config{
        .data_directory = root->path(),
        .buffer_pool_pages = 4U,
        .wal_ring_pages = 4U,
    };
    auto database = Database::create(config);
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

    ASSERT_TRUE(assigned.close());
    ASSERT_TRUE(database->close());
}

}  // namespace
}  // namespace db::api
