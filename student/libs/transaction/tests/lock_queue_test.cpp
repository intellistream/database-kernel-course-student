#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/transaction/lock_manager.hpp"

#include "lock_manager_state.hpp"
#include "lock_queue.hpp"

namespace db::transaction::detail {
namespace {

auto request(base::Xid xid, const LockTarget& target, TableLockMode mode)
    -> std::shared_ptr<LockRequest> {
    auto result = std::make_shared<LockRequest>();
    result->xid = xid;
    result->target = target;
    result->mode = static_cast<std::uint8_t>(mode);
    return result;
}

TEST(LockQueueTest, ReleaseGrantsOnlyTheCompatibleFifoPrefix) {
    constexpr auto k_first = base::Xid{1};
    constexpr auto k_second = base::Xid{2};
    constexpr auto k_third = base::Xid{3};
    const LockTarget target{.kind = LockTargetKind::kTable, .relation_id = base::RelationId{30}};

    LockTable locks;
    auto& state = locks[target];
    state.holders.emplace(k_first, static_cast<std::uint8_t>(TableLockMode::kAccessExclusive));
    const auto second = request(k_second, target, TableLockMode::kShare);
    const auto third = request(k_third, target, TableLockMode::kRowExclusive);
    state.waiters.push_back(second);
    state.waiters.push_back(third);
    GrantedLockOwners granted{{k_first, {target}}};
    WaitingLockOwners owners{{k_second, second}, {k_third, third}};

    ASSERT_TRUE(release_lock_owner(locks, granted, owners, k_first));
    EXPECT_TRUE(second->granted);
    EXPECT_FALSE(third->granted);
    EXPECT_EQ(locks.at(target).waiters.size(), 1U);

    ASSERT_TRUE(release_lock_owner(locks, granted, owners, k_second));
    EXPECT_TRUE(third->granted);
    EXPECT_TRUE(owners.empty());
}

TEST(LockQueueTest, CancellationPublishesTerminalStateBeforeReturning) {
    constexpr auto k_holder = base::Xid{1};
    constexpr auto k_cancelled = base::Xid{2};
    constexpr auto k_follower = base::Xid{3};
    const LockTarget target{.kind = LockTargetKind::kTable, .relation_id = base::RelationId{31}};

    LockState state;
    state.holders.emplace(k_holder, static_cast<std::uint8_t>(TableLockMode::kRowExclusive));
    const auto cancelled = request(k_cancelled, target, TableLockMode::kShare);
    const auto follower = request(k_follower, target, TableLockMode::kAccessShare);
    state.waiters.push_back(cancelled);
    state.waiters.push_back(follower);
    GrantedLockOwners granted;
    WaitingLockOwners owners{{k_cancelled, cancelled}, {k_follower, follower}};

    cancel_lock_request(state, granted, owners, cancelled);
    EXPECT_TRUE(cancelled->cancelled);
    EXPECT_TRUE(follower->granted);
    EXPECT_FALSE(owners.contains(k_cancelled));
    EXPECT_FALSE(owners.contains(k_follower));
    ASSERT_TRUE(granted.contains(k_follower));
    EXPECT_EQ(granted.at(k_follower), std::vector<LockTarget>{target});
}

}  // namespace
}  // namespace db::transaction::detail
