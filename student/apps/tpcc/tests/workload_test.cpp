#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include <gtest/gtest.h>

#include "array_access.hpp"
#include "transaction_common.hpp"
#include "workload.hpp"

namespace db::tpcc {
namespace {

TEST(TpccWorkloadTest, EveryDeckHasTheExactRankedMix) {
    TransactionGenerator generator{5U, 2U, 7U};
    std::array<std::size_t, 5> counts{};
    for (std::size_t index = 0U; index < 200U; ++index) {
        ++array_element(counts, transaction_index(generator.next_kind()));
    }
    EXPECT_EQ(counts, (std::array<std::size_t, 5>{90U, 86U, 8U, 8U, 8U}));
}

TEST(TpccWorkloadTest, ClientStreamsAreReproducibleAndSeparate) {
    TransactionGenerator first{9U, 1U, 0U};
    TransactionGenerator same{9U, 1U, 0U};
    TransactionGenerator other{9U, 1U, 1U};
    std::vector<TransactionKind> left;
    std::vector<TransactionKind> equal;
    std::vector<TransactionKind> distinct;
    for (std::size_t index = 0U; index < 200U; ++index) {
        left.push_back(first.next_kind());
        equal.push_back(same.next_kind());
        distinct.push_back(other.next_kind());
    }
    EXPECT_EQ(left, equal);
    EXPECT_NE(left, distinct);
}

TEST(TpccWorkloadTest, HotspotRouletteUsesTheRankedWeights) {
    const WorkloadRouting routing = WorkloadRouting::derive(777U);
    const PhaseRouting phase = routing.phase(3U);
    std::map<std::uint32_t, std::size_t> counts;
    for (const std::uint32_t warehouse : phase.roulette()) {
        ++counts[warehouse];
    }
    ASSERT_EQ(counts.size(), 50U);
    ASSERT_EQ(phase.roulette().size(), 160U);
    EXPECT_EQ(routing.hot_items().size(), 24U);
    for (std::uint32_t warehouse = 1U; warehouse <= 50U; ++warehouse) {
        if (routing.is_hot(warehouse)) {
            EXPECT_EQ(counts[warehouse], 26U);
        } else {
            EXPECT_GE(counts[warehouse], 1U);
            EXPECT_LE(counts[warehouse], 2U);
        }
    }
}

TEST(TpccWorkloadTest, FiveClientWavesCoverEveryRouletteSlotOnce) {
    const PhaseRouting phase = WorkloadRouting::derive(9U).phase(1U);
    std::array<std::size_t, 160> counts{};
    for (std::uint64_t transaction = 0U; transaction < 5U; ++transaction) {
        for (std::uint32_t client = 0U; client < 32U; ++client) {
            ++array_element(counts, phase.slot_index(client, transaction));
        }
    }
    for (const std::size_t count : counts) {
        EXPECT_EQ(count, 1U);
    }
}

TEST(TpccWorkloadTest, UsesTheFixedLastNameAndStockRules) {
    EXPECT_EQ(tpcc_last_name(0U), "BARRBARRBARR");
    EXPECT_EQ(tpcc_last_name(123U), "OUGHABLEPRII");
    EXPECT_EQ(tpcc_last_name(999U), "EINGEINGEING");
    EXPECT_EQ(next_stock_quantity(15, 5), 10);
    EXPECT_EQ(next_stock_quantity(10, 5), 96);
}

}  // namespace
}  // namespace db::tpcc
