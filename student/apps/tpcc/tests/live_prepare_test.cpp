#include <chrono>
#include <cstdint>
#include <string_view>

#include <gtest/gtest.h>

#include "db/io/socket.hpp"
#include "db/protocol/rmdb_client.hpp"

#include "prepared.hpp"

namespace db::tpcc {
namespace {

TEST(TpccLivePrepareTest, LoadedCanonicalServerAcceptsTheRankedDictionary) {
    auto endpoint = io::TcpEndpoint::parse(std::string_view{DB_TPCC_LIVE_ADDRESS});
    ASSERT_TRUE(endpoint) << endpoint.error().to_string();
    ASSERT_NE(endpoint->port, 0U);

    auto client = protocol::RmdbClient::connect(
        endpoint.value(),
        protocol::RmdbClientOptions{
            .io_timeout = std::chrono::seconds{30},
            .maximum_response_bytes = protocol::kMaximumRmdbFramePayloadSize,
        }
    );
    ASSERT_TRUE(client) << client.error().to_string();

    auto health = client->execute("select COUNT(*) from warehouse;");
    ASSERT_TRUE(health) << health.error().to_string();
    ASSERT_EQ(health->terminal.kind, protocol::RmdbTerminalKind::kResultEnd);
    ASSERT_EQ(health->rows.size(), 1U);
    ASSERT_EQ(health->rows.front().size(), 1U);
    const std::int64_t* const warehouse_count = health->rows.front().front().as_integer();
    ASSERT_NE(warehouse_count, nullptr);
    ASSERT_EQ(*warehouse_count, 50);

    auto isolation = client->execute("SET TRANSACTION ISOLATION LEVEL SNAPSHOT ISOLATION;");
    ASSERT_TRUE(isolation) << isolation.error().to_string();
    ASSERT_EQ(isolation->terminal.kind, protocol::RmdbTerminalKind::kCommandOk)
        << isolation->terminal.diagnostic;

    const auto statements = statement::definitions();
    auto catalog = client->prepare_set(statements);
    ASSERT_TRUE(catalog) << catalog.error().to_string();
    EXPECT_EQ(catalog->entries().size(), statement::kRankedStatementCount);
    EXPECT_TRUE(client->close());
}

}  // namespace
}  // namespace db::tpcc
