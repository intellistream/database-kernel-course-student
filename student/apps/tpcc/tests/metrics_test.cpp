#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "array_access.hpp"
#include "metrics.hpp"
#include "prepared.hpp"
#include "report.hpp"
#include "transaction_common.hpp"
#include "workload.hpp"

namespace db::tpcc {
namespace {

TEST(TpccMetricsTest, MergesTransactionCountersByKind) {
    TransactionCounters left;
    TransactionCounters right;
    array_element(left.attempted, 0U) = 3U;
    array_element(left.committed, 0U) = 2U;
    left.expected_rollbacks = 1U;
    array_element(right.attempted, 1U) = 4U;
    array_element(right.committed, 1U) = 3U;
    array_element(right.abandoned, 1U) = 1U;
    array_element(right.database_aborts, 1U) = 5U;
    right.retries = 4U;

    left.merge(right);

    EXPECT_EQ(left.total_committed(), 5U);
    EXPECT_EQ(array_element(left.attempted, 1U), 4U);
    EXPECT_EQ(array_element(left.database_aborts, 1U), 5U);
    EXPECT_EQ(left.retries, 4U);
    EXPECT_TRUE(validate_counter_accounting(left, "test"));
}

TEST(TpccMetricsTest, AttributesBatchLatencyAcrossAttemptedStatements) {
    const std::vector<protocol::RmdbBatchOperation> operations{
        operation(statement::kBegin),
        operation(statement::kNewOrderCustomer),
    };
    const protocol::RmdbBatchResponse response{
        .executed_operation_count = 2U,
        .responses = {},
        .terminal = protocol::RmdbBatchTerminal{
            .kind = protocol::RmdbBatchTerminalKind::kOk,
            .failed_operation = 0U,
            .diagnostic = {},
        },
    };
    SqlTelemetry first;
    first.record(operations, response, std::chrono::nanoseconds{101});
    SqlTelemetry second;
    second.record(operations, response, std::chrono::nanoseconds{99});
    first.merge(std::move(second));

    SqlProfile profile = std::move(first).into_profile();
    ASSERT_EQ(profile.total_batch_count, 2U);
    ASSERT_EQ(profile.total_batch_wait_ns, 200U);
    ASSERT_EQ(profile.batches.size(), 1U);
    EXPECT_EQ(profile.batches.front().count, 2U);
    ASSERT_EQ(profile.statements.size(), 2U);
    EXPECT_EQ(profile.statements.front().attempted, 2U);
    EXPECT_EQ(
        profile.statements.front().attributed_total_ns
            + profile.statements.back().attributed_total_ns,
        200U
    );
}

TEST(TpccMetricsTest, UsesTheExactFloat32DeliverySumDomain) {
    const auto first = exact_float32_units(1.01F);
    const auto second = exact_float32_units(2.01F);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    const auto sum = exact_float32_sum(first.value() + second.value());
    ASSERT_TRUE(sum.has_value());
    EXPECT_EQ(canonical_float_bits(sum.value()), canonical_float_bits(3.02F));

    const float half_unit = std::bit_cast<float>(std::uint32_t{0x3ba3d70bU});
    EXPECT_FALSE(exact_float32_units(half_unit).has_value());
    EXPECT_FALSE(exact_float32_sum((std::uint64_t{1U} << 53U) + 1U).has_value());
}

TEST(TpccMetricsTest, AcceptsAReconciledRoundAndExactDeckMix) {
    RoundMetrics round;
    round.phase_id = 2U;
    for (std::size_t index = 0U; index < round.counters.attempted.size(); ++index) {
        array_element(round.counters.attempted, index) = 1U;
        array_element(round.counters.committed, index) = 1U;
    }
    round.counters.delivery_processed_orders = 1U;
    round.covered_warehouses = 1U;
    array_element(round.completed_by_warehouse, 0U) = 5U;

    const WorkloadRouting routing = WorkloadRouting::derive(12U);
    EXPECT_TRUE(validate_round(round, routing));

    TransactionCounters lifetime;
    constexpr std::array<std::uint64_t, 5> deck_counts{900U, 860U, 80U, 80U, 80U};
    for (std::size_t index = 0U; index < deck_counts.size(); ++index) {
        array_element(lifetime.attempted, index) = array_element(deck_counts, index);
        array_element(lifetime.committed, index) = array_element(deck_counts, index);
    }
    EXPECT_TRUE(validate_mix(lifetime));
}

}  // namespace
}  // namespace db::tpcc
