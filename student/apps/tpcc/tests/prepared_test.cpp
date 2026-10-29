#include <algorithm>
#include <cstdint>
#include <set>

#include <gtest/gtest.h>

#include "prepared.hpp"

namespace db::tpcc::statement {
namespace {

TEST(TpccPreparedTest, DefinesTheExactSortedRankedDictionary) {
    const auto statements = definitions();
    ASSERT_EQ(statements.size(), kRankedStatementCount);
    std::set<std::uint16_t> identifiers;
    for (const auto& statement : statements) {
        EXPECT_TRUE(identifiers.insert(statement.statement_id).second);
        EXPECT_FALSE(statement.sql.empty());
    }
    EXPECT_TRUE(
        std::ranges::is_sorted(statements, {}, &protocol::RmdbPreparedStatement::statement_id)
    );
    EXPECT_EQ(stock_district(1U), 17U);
    EXPECT_EQ(stock_district(10U), 26U);
    EXPECT_EQ(name(kStockLevelCount), std::string_view{"stock_level_count"});
}

TEST(TpccPreparedTest, RetainsTheDependencyBatchShapes) {
    const auto statements = definitions();
    const auto find = [&statements](const std::uint16_t id) -> const auto& {
        return *std::ranges::find(statements, id, &protocol::RmdbPreparedStatement::statement_id);
    };
    EXPECT_EQ(find(kBegin).result_kind, protocol::RmdbPreparedResultKind::kCommand);
    EXPECT_TRUE(find(kBegin).parameter_types.empty());
    EXPECT_EQ(find(kNewOrderCustomer).parameter_types.size(), 3U);
    EXPECT_EQ(find(kNewOrderCustomer).result_types.size(), 4U);
    EXPECT_EQ(find(kPaymentCustomerByLast).result_types.size(), 7U);
    EXPECT_EQ(find(kDeliveryLines).result_types.size(), 3U);
    EXPECT_EQ(find(kStockLevelCount).result_types.size(), 1U);
}

}  // namespace
}  // namespace db::tpcc::statement
