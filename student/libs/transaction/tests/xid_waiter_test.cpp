#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/transaction/lock_manager.hpp"
#include "db/transaction/manager.hpp"
#include "db/transaction/xid_waiter.hpp"

namespace db::transaction {
namespace {

using namespace std::chrono_literals;

void wait_until_registered(const XidWaiter& waiter, const std::size_t expected) {
    for (;;) {
        if (waiter.stats().waiting_transactions == expected) {
            return;
        }
        std::this_thread::yield();
    }
}

TEST(XidWaiterTest, TransactionCompletionWakesAllRegisteredWaiters) {
    TransactionManager manager;
    auto blocker_lease = manager.register_session();
    ASSERT_TRUE(blocker_lease);
    auto first_waiter_lease = manager.register_session();
    ASSERT_TRUE(first_waiter_lease);
    auto second_waiter_lease = manager.register_session();
    ASSERT_TRUE(second_waiter_lease);
    auto blocker = manager.assign_xid(*blocker_lease);
    auto first_waiter = manager.assign_xid(*first_waiter_lease);
    auto second_waiter = manager.assign_xid(*second_waiter_lease);
    ASSERT_TRUE(blocker);
    ASSERT_TRUE(first_waiter);
    ASSERT_TRUE(second_waiter);
    std::atomic<std::size_t> completed{0U};
    std::atomic<std::size_t> failed{0U};

    std::thread first{[&] {
        auto waited = manager.xid_waiter().wait_for(*blocker, *first_waiter);
        (waited ? completed : failed).fetch_add(1U, std::memory_order_relaxed);
    }};
    std::thread second{[&] {
        auto waited = manager.xid_waiter().wait_for(*blocker, *second_waiter);
        (waited ? completed : failed).fetch_add(1U, std::memory_order_relaxed);
    }};
    wait_until_registered(manager.xid_waiter(), 2U);
    EXPECT_EQ(manager.xid_waiter().stats().blocking_transactions, 1U);
    ASSERT_TRUE(manager.complete(*blocker_lease, *blocker, TransactionStatus::kCommitted));
    first.join();
    second.join();

    EXPECT_EQ(completed.load(std::memory_order_relaxed), 2U);
    EXPECT_EQ(failed.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(manager.xid_waiter().stats(), XidWaiterStats{});
    ASSERT_TRUE(manager.complete(*first_waiter_lease, *first_waiter, TransactionStatus::kAborted));
    ASSERT_TRUE(
        manager.complete(*second_waiter_lease, *second_waiter, TransactionStatus::kAborted)
    );
    ASSERT_TRUE(blocker_lease->close());
    ASSERT_TRUE(first_waiter_lease->close());
    ASSERT_TRUE(second_waiter_lease->close());
}

TEST(XidWaiterTest, RejectsTheEdgeThatWouldCloseAWaitForCycle) {
    TransactionManager manager{LockManagerOptions{.deadlock_timeout = 10ms}};
    auto first_lease = manager.register_session();
    auto second_lease = manager.register_session();
    ASSERT_TRUE(first_lease);
    ASSERT_TRUE(second_lease);
    auto first_xid = manager.assign_xid(*first_lease);
    auto second_xid = manager.assign_xid(*second_lease);
    ASSERT_TRUE(first_xid);
    ASSERT_TRUE(second_xid);
    const auto& waiter = manager.xid_waiter();
    std::atomic<bool> first_succeeded{false};
    std::thread first{[&] {
        first_succeeded.store(
            static_cast<bool>(waiter.wait_for(*second_xid, *first_xid)),
            std::memory_order_release
        );
    }};
    wait_until_registered(waiter, 1U);

    auto cycle = waiter.wait_for(*first_xid, *second_xid);
    ASSERT_FALSE(cycle);
    EXPECT_EQ(cycle.error().code(), base::ErrorCode::kDeadlockDetected);
    ASSERT_TRUE(manager.complete(*second_lease, *second_xid, TransactionStatus::kAborted));
    first.join();
    EXPECT_TRUE(first_succeeded.load(std::memory_order_acquire));
    EXPECT_EQ(waiter.stats(), XidWaiterStats{});
    ASSERT_TRUE(manager.complete(*first_lease, *first_xid, TransactionStatus::kAborted));
    ASSERT_TRUE(first_lease->close());
    ASSERT_TRUE(second_lease->close());
}

TEST(XidWaiterTest, TransactionTagReleaseIsTheOnlyWakeupBoundary) {
    TransactionManager manager;
    auto waiter_lease = manager.register_session();
    auto blocker_lease = manager.register_session();
    ASSERT_TRUE(waiter_lease);
    ASSERT_TRUE(blocker_lease);
    auto waiter_xid = manager.assign_xid(*waiter_lease);
    auto blocker_xid = manager.assign_xid(*blocker_lease);
    ASSERT_TRUE(waiter_xid);
    ASSERT_TRUE(blocker_xid);
    const auto& waiter = manager.xid_waiter();
    std::atomic<bool> completed{false};
    std::atomic<bool> observed_final_status{false};
    std::atomic<bool> observed_cleared_slot{false};
    std::thread waiting{[&] {
        const auto status = waiter.wait_for(*blocker_xid, *waiter_xid);
        if (status) {
            observed_final_status.store(
                manager.commit_log().is_committed(*blocker_xid),
                std::memory_order_relaxed
            );
            observed_cleared_slot.store(
                !blocker_lease->slot()->has_active_transaction(),
                std::memory_order_relaxed
            );
        }
        completed.store(static_cast<bool>(status), std::memory_order_release);
    }};
    wait_until_registered(waiter, 1U);

    ASSERT_TRUE(manager.lock_manager().release_all(*blocker_xid));
    std::this_thread::sleep_for(75ms);
    EXPECT_FALSE(completed.load(std::memory_order_acquire));
    ASSERT_TRUE(manager.complete(*blocker_lease, *blocker_xid, TransactionStatus::kCommitted));
    waiting.join();

    EXPECT_TRUE(completed.load(std::memory_order_acquire));
    EXPECT_TRUE(observed_final_status.load(std::memory_order_relaxed));
    EXPECT_TRUE(observed_cleared_slot.load(std::memory_order_relaxed));
    EXPECT_EQ(waiter.stats(), XidWaiterStats{});
    ASSERT_TRUE(manager.complete(*waiter_lease, *waiter_xid, TransactionStatus::kAborted));
    ASSERT_TRUE(waiter_lease->close());
    ASSERT_TRUE(blocker_lease->close());
}

}  // namespace
}  // namespace db::transaction
