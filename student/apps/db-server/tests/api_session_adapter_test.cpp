#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "db/api/database.hpp"
#include "db/base/error.hpp"
#include "db/protocol/session_port.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/types/value.hpp"

#include "api_session_adapter.hpp"

namespace db::server {
namespace {

auto test_config(const std::filesystem::path& path) -> api::DatabaseConfig {
    return api::DatabaseConfig{
        .data_directory = path,
        .buffer_pool_pages = 8U,
        .wal_ring_pages = 8U,
    };
}

TEST(ApiSessionAdapterTest, MapsRealDatabaseSessionsPreparedStatementsAndWireValues) {
    auto root = test_support::TestDirectory::create("server-adapter");
    ASSERT_TRUE(root) << root.error().to_string();
    auto created = api::Database::create(test_config(root->path()));
    ASSERT_TRUE(created) << created.error().to_string();
    auto database = std::make_shared<api::Database>(std::move(created).value());
    ApiSessionFactory factory{database};
    auto session = factory.open_session();
    ASSERT_TRUE(session) << session.error().to_string();

    auto created_table = (*session)->execute_stream("CREATE TABLE items (id INT, label CHAR(8))");
    ASSERT_TRUE(created_table) << created_table.error().to_string();
    EXPECT_EQ(created_table->kind(), protocol::ExecutionKind::kCommand);

    auto legacy_index = (*session)->execute_stream("CREATE INDEX items(id)");
    ASSERT_TRUE(legacy_index) << legacy_index.error().to_string();
    EXPECT_EQ(legacy_index->kind(), protocol::ExecutionKind::kCommand);

    auto inserted = (*session)->execute_stream("INSERT INTO items VALUES (1, 'one'), (2, 'two')");
    ASSERT_TRUE(inserted) << inserted.error().to_string();
    EXPECT_EQ(inserted->kind(), protocol::ExecutionKind::kCommand);

    auto script = (*session)->execute_stream(
        "INSERT INTO items VALUES (3, 'three'); INSERT INTO items VALUES (4, 'four')"
    );
    ASSERT_FALSE(script);
    EXPECT_EQ(script.error().code(), base::ErrorCode::kUnsupported);

    auto empty_table = (*session)->execute_stream("CREATE TABLE empty_items (id INT)");
    ASSERT_TRUE(empty_table) << empty_table.error().to_string();
    auto empty_sum = (*session)->execute_stream("SELECT SUM(id) AS total FROM empty_items");
    ASSERT_TRUE(empty_sum) << empty_sum.error().to_string();
    ASSERT_EQ(empty_sum->rows().size(), 1U);
    ASSERT_EQ(empty_sum->rows()[0U].size(), 1U);
    EXPECT_EQ(empty_sum->rows()[0U][0U].integer(), 0);

    auto checkpoint = (*session)->execute_stream("CREATE STATIC_CHECKPOINT");
    ASSERT_TRUE(checkpoint) << checkpoint.error().to_string();
    EXPECT_EQ(checkpoint->kind(), protocol::ExecutionKind::kCommand);

    auto queried = (*session)->execute_stream("SELECT id, label FROM items ORDER BY id");
    ASSERT_TRUE(queried) << queried.error().to_string();
    ASSERT_EQ(queried->kind(), protocol::ExecutionKind::kQuery);
    ASSERT_EQ(queried->columns().size(), 2U);
    EXPECT_EQ(queried->columns()[0U].name, "id");
    EXPECT_EQ(queried->columns()[0U].data_type, protocol::WireDataType::kInt32);
    EXPECT_EQ(queried->columns()[1U].name, "label");
    EXPECT_EQ(queried->columns()[1U].data_type, protocol::WireDataType::kChar);
    ASSERT_EQ(queried->rows().size(), 2U);
    EXPECT_EQ(queried->rows()[0U][0U].integer(), 1);
    EXPECT_EQ(queried->rows()[1U][1U].text(), "two");

    const std::array<protocol::WireDataType, 1U> parameter_types{
        protocol::WireDataType::kInt32,
    };
    auto prepared = (*session)->prepare("SELECT label FROM items WHERE id = $1", parameter_types);
    ASSERT_TRUE(prepared) << prepared.error().to_string();
    ASSERT_EQ((*prepared)->result_columns().size(), 1U);
    EXPECT_EQ((*prepared)->result_columns()[0U].data_type, protocol::WireDataType::kChar);

    const std::array<types::Value, 1U> parameters{types::Value::integer(2)};
    auto prepared_result = (*prepared)->execute(parameters);
    ASSERT_TRUE(prepared_result) << prepared_result.error().to_string();
    ASSERT_EQ(prepared_result->kind(), protocol::ExecutionKind::kQuery);
    ASSERT_EQ(prepared_result->rows().size(), 1U);
    EXPECT_EQ(prepared_result->rows()[0U][0U].text(), "two");

    auto isolation =
        (*session)->execute_stream("SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION");
    ASSERT_TRUE(isolation) << isolation.error().to_string();
    EXPECT_EQ(isolation->kind(), protocol::ExecutionKind::kCommand);
    auto began = (*session)->execute_stream("BEGIN");
    ASSERT_TRUE(began) << began.error().to_string();
    auto rolled_back = (*session)->execute_stream("ROLLBACK");
    ASSERT_TRUE(rolled_back) << rolled_back.error().to_string();

    auto load_table = (*session)->execute_stream("CREATE TABLE load_items (id INT, label CHAR(8))");
    ASSERT_TRUE(load_table) << load_table.error().to_string();
    auto load_index =
        (*session)->execute_stream("CREATE UNIQUE INDEX load_items_pk ON load_items (id)");
    ASSERT_TRUE(load_index) << load_index.error().to_string();
    const auto csv_path = root->path() / "load_items.csv";
    std::ofstream csv{csv_path};
    ASSERT_TRUE(csv.is_open());
    csv << "id,label\n1,one\n2,two\n";
    csv.close();

    const std::string load_sql = "LOAD " + csv_path.string() + " INTO load_items";
    auto load = (*session)->execute_stream(load_sql);
    ASSERT_TRUE(load) << load.error().to_string();
    EXPECT_EQ(load->kind(), protocol::ExecutionKind::kCommand);
    auto loaded = (*session)->execute_stream("SELECT COUNT(*) FROM load_items");
    ASSERT_TRUE(loaded) << loaded.error().to_string();
    ASSERT_EQ(loaded->rows().size(), 1U);
    ASSERT_EQ(loaded->rows().front().size(), 1U);
    EXPECT_EQ(loaded->rows().front().front().integer(), 2);

    auto malformed_load = (*session)->execute_stream("LOAD /tmp/items.csv ONTO load_items");
    ASSERT_FALSE(malformed_load);
    EXPECT_EQ(malformed_load.error().code(), base::ErrorCode::kInvalidArgument);

    prepared->reset();
    ASSERT_TRUE((*session)->close());
    session->reset();
    ASSERT_TRUE(database->close());
}

TEST(ApiSessionAdapterTest, RejectsAResultThatWouldNarrowOnTheWire) {
    auto root = test_support::TestDirectory::create("server-adapter-narrow");
    ASSERT_TRUE(root) << root.error().to_string();
    auto created = api::Database::create(test_config(root->path()));
    ASSERT_TRUE(created) << created.error().to_string();
    auto database = std::make_shared<api::Database>(std::move(created).value());
    ApiSessionFactory factory{database};
    auto session = factory.open_session();
    ASSERT_TRUE(session) << session.error().to_string();

    auto result = (*session)->execute_stream("SELECT CAST(2147483648 AS INT) AS too_large");
    EXPECT_FALSE(result);
    if (!result) {
        EXPECT_EQ(result.error().code(), base::ErrorCode::kOutOfRange);
    }

    ASSERT_TRUE((*session)->close());
    session->reset();
    ASSERT_TRUE(database->close());
}

}  // namespace
}  // namespace db::server
