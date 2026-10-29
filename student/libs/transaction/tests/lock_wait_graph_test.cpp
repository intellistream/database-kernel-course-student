#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/transaction/lock_manager.hpp"

#include "lock_manager_state.hpp"
#include "lock_wait_graph.hpp"

namespace db::transaction::detail {
namespace {

auto request(
    base::Xid xid,
    const LockTarget& target,
    TableLockMode mode = TableLockMode::kAccessExclusive
) -> std::shared_ptr<LockRequest> {
    auto result = std::make_shared<LockRequest>();
    result->xid = xid;
    result->target = target;
    result->mode = static_cast<std::uint8_t>(mode);
    return result;
}

TEST(LockWaitGraphTest, DetectsHolderCycleFromTheRequester) {
    constexpr auto k_first = base::Xid{1};
    constexpr auto k_second = base::Xid{2};
    const LockTarget first_target{
        .kind = LockTargetKind::kTable,
        .relation_id = base::RelationId{10}
    };
    const LockTarget second_target{
        .kind = LockTargetKind::kTable,
        .relation_id = base::RelationId{11}
    };

    LockTable locks;
    locks[first_target].holders.emplace(
        k_first,
        static_cast<std::uint8_t>(TableLockMode::kAccessExclusive)
    );
    locks[first_target].waiters.push_back(request(k_second, first_target));
    locks[second_target].holders.emplace(
        k_second,
        static_cast<std::uint8_t>(TableLockMode::kAccessExclusive)
    );
    locks[second_target].waiters.push_back(request(k_first, second_target));

    EXPECT_TRUE(lock_wait_graph_has_cycle(locks, k_first));
    EXPECT_TRUE(lock_wait_graph_has_cycle(locks, k_second));
}

TEST(LockWaitGraphTest, IncludesFifoDependencyWithoutInventingACycle) {
    constexpr auto k_holder = base::Xid{1};
    constexpr auto k_first_waiter = base::Xid{2};
    constexpr auto k_second_waiter = base::Xid{3};
    const LockTarget target{.kind = LockTargetKind::kTable, .relation_id = base::RelationId{20}};

    LockTable locks;
    auto& state = locks[target];
    state.holders.emplace(k_holder, static_cast<std::uint8_t>(TableLockMode::kAccessExclusive));
    state.waiters.push_back(request(k_first_waiter, target));
    state.waiters.push_back(request(k_second_waiter, target, TableLockMode::kAccessShare));

    EXPECT_FALSE(lock_wait_graph_has_cycle(locks, k_holder));
    EXPECT_FALSE(lock_wait_graph_has_cycle(locks, k_first_waiter));
    EXPECT_FALSE(lock_wait_graph_has_cycle(locks, k_second_waiter));
}

TEST(LockTargetShardTest, CorrelatedDisjointTableAndRowTargetsDoNotSharePartitions) {
    std::array<std::size_t, kLockTargetShardCount> occupancy{};
    for (std::uint32_t thread = 0U; thread < 32U; ++thread) {
        const auto relation = base::RelationId{8U + thread};
        const LockTarget table{
            .kind = LockTargetKind::kTable,
            .relation_id = relation,
        };
        const LockTarget row{
            .kind = LockTargetKind::kRow,
            .relation_id = relation,
            .rid = base::Rid{base::PageId{thread + 1U}, base::SlotId{1U}},
        };
        ++occupancy.at(lock_target_shard_index(table));
        ++occupancy.at(lock_target_shard_index(row));
        EXPECT_NE(lock_target_shard_index(table), lock_target_shard_index(row));
    }

    EXPECT_EQ(
        std::ranges::count_if(
            occupancy,
            [](const auto count) {
                return count != 0U;
            }
        ),
        48
    );
    EXPECT_EQ(*std::ranges::max_element(occupancy), 2U);
}

TEST(LockTargetShardTest, SharedTableRowAndTransactionTagsUseSeparateDomains) {
    const LockTarget table{
        .kind = LockTargetKind::kTable,
        .relation_id = base::RelationId{7U},
    };
    const LockTarget row{
        .kind = LockTargetKind::kRow,
        .relation_id = base::RelationId{7U},
        .rid = base::Rid{base::PageId{1U}, base::SlotId{1U}},
    };
    const LockTarget transaction{
        .kind = LockTargetKind::kTransaction,
        .transaction_xid = base::Xid{7U},
    };

    EXPECT_LT(lock_target_shard_index(table), kTransactionLockShardBase);
    EXPECT_GE(lock_target_shard_index(transaction), kTransactionLockShardBase);
    EXPECT_LT(lock_target_shard_index(transaction), kRowLockShardBase);
    EXPECT_GE(lock_target_shard_index(row), kRowLockShardBase);
}

}  // namespace
}  // namespace db::transaction::detail
