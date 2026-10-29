#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/transaction/lock_manager.hpp"
#include "db/transaction/manager.hpp"

namespace db::transaction {
namespace {

using namespace std::chrono_literals;

auto wait_for_waiters(const LockManager& manager, std::size_t expected) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (manager.stats().waiting_requests == expected) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return manager.stats().waiting_requests == expected;
}

auto wait_for_flag(const std::atomic<bool>& flag) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (flag.load(std::memory_order_acquire)) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return flag.load(std::memory_order_acquire);
}

TEST(LockManagerTest, ExposesStableModeNamesCompatibilityAndUpgradeOrder) {
    constexpr std::array k_table_modes{
        TableLockMode::kAccessShare,
        TableLockMode::kRowShare,
        TableLockMode::kRowExclusive,
        TableLockMode::kShareUpdateExclusive,
        TableLockMode::kShare,
        TableLockMode::kShareRowExclusive,
        TableLockMode::kExclusive,
        TableLockMode::kAccessExclusive,
    };
    constexpr std::array k_table_compatibility{
        std::array{true, true, true, true, true, true, true, false},
        std::array{true, true, true, true, true, true, false, false},
        std::array{true, true, true, true, false, false, false, false},
        std::array{true, true, true, false, false, false, false, false},
        std::array{true, true, false, false, true, false, false, false},
        std::array{true, true, false, false, false, false, false, false},
        std::array{true, false, false, false, false, false, false, false},
        std::array{false, false, false, false, false, false, false, false},
    };
    for (std::size_t left = 0; left < k_table_modes.size(); ++left) {
        for (std::size_t right = 0; right < k_table_modes.size(); ++right) {
            EXPECT_EQ(
                table_lock_modes_compatible(k_table_modes.at(left), k_table_modes.at(right)),
                k_table_compatibility.at(left).at(right)
            );
        }
    }

    constexpr std::array k_row_modes{
        RowLockMode::kKeyShare,
        RowLockMode::kShare,
        RowLockMode::kNoKeyExclusive,
        RowLockMode::kExclusive,
    };
    constexpr std::array k_row_compatibility{
        std::array{true, true, true, false},
        std::array{true, true, false, false},
        std::array{true, false, false, false},
        std::array{false, false, false, false},
    };
    for (std::size_t left = 0; left < k_row_modes.size(); ++left) {
        for (std::size_t right = 0; right < k_row_modes.size(); ++right) {
            EXPECT_EQ(
                row_lock_modes_compatible(k_row_modes.at(left), k_row_modes.at(right)),
                k_row_compatibility.at(left).at(right)
            );
        }
    }

    EXPECT_EQ(table_lock_mode_name(TableLockMode::kShareUpdateExclusive), "share_update_exclusive");
    EXPECT_EQ(row_lock_mode_name(RowLockMode::kNoKeyExclusive), "no_key_exclusive");
    // Fixed-width unknown values exercise the stable rejection/name fallback contracts.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_EQ(table_lock_mode_name(static_cast<TableLockMode>(255)), "unknown");
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    EXPECT_EQ(row_lock_mode_name(static_cast<RowLockMode>(255)), "unknown");
    EXPECT_TRUE(
        table_lock_mode_subsumes(TableLockMode::kAccessExclusive, TableLockMode::kAccessShare)
    );
    EXPECT_TRUE(table_lock_mode_subsumes(TableLockMode::kRowExclusive, TableLockMode::kRowShare));
    EXPECT_FALSE(table_lock_mode_subsumes(TableLockMode::kShare, TableLockMode::kRowExclusive));
    EXPECT_TRUE(row_lock_mode_subsumes(RowLockMode::kShare, RowLockMode::kKeyShare));
    EXPECT_TRUE(row_lock_mode_subsumes(RowLockMode::kNoKeyExclusive, RowLockMode::kShare));
}

TEST(LockManagerTest, ValidatesTargetsIdempotenceAndExplicitUpgrades) {
    LockManager manager;
    constexpr auto k_xid = base::Xid{1};
    constexpr auto k_relation = base::RelationId{9};
    constexpr auto k_rid = base::Rid{base::PageId{3}, base::SlotId{4}};

    auto invalid_xid =
        manager.acquire_table(base::kInvalidXid, k_relation, TableLockMode::kAccessShare);
    ASSERT_FALSE(invalid_xid);
    EXPECT_EQ(invalid_xid.error().code(), base::ErrorCode::kInvalidArgument);
    auto invalid_relation =
        manager.acquire_table(k_xid, base::kInvalidRelationId, TableLockMode::kAccessShare);
    ASSERT_FALSE(invalid_relation);
    EXPECT_EQ(invalid_relation.error().code(), base::ErrorCode::kInvalidArgument);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) -- rejection input.
    auto invalid_mode = manager.acquire_table(k_xid, k_relation, static_cast<TableLockMode>(255));
    ASSERT_FALSE(invalid_mode);
    EXPECT_EQ(invalid_mode.error().code(), base::ErrorCode::kInvalidArgument);
    ASSERT_TRUE(manager.acquire_table(k_xid, k_relation, TableLockMode::kRowShare));
    ASSERT_TRUE(manager.acquire_table(k_xid, k_relation, TableLockMode::kRowShare));
    auto implicit_upgrade = manager.acquire_table(k_xid, k_relation, TableLockMode::kRowExclusive);
    ASSERT_FALSE(implicit_upgrade);
    EXPECT_EQ(implicit_upgrade.error().code(), base::ErrorCode::kTransactionState);
    auto downgrade = manager.upgrade_table(k_xid, k_relation, TableLockMode::kAccessShare);
    ASSERT_FALSE(downgrade);
    EXPECT_EQ(downgrade.error().code(), base::ErrorCode::kInvalidArgument);
    ASSERT_TRUE(manager.upgrade_table(k_xid, k_relation, TableLockMode::kRowExclusive));
    EXPECT_EQ(manager.held_table_mode(k_xid, k_relation), TableLockMode::kRowExclusive);

    auto invalid_row =
        manager.acquire_row(k_xid, k_relation, base::kInvalidRid, RowLockMode::kKeyShare);
    ASSERT_FALSE(invalid_row);
    EXPECT_EQ(invalid_row.error().code(), base::ErrorCode::kInvalidArgument);
    ASSERT_TRUE(manager.acquire_row(k_xid, k_relation, k_rid, RowLockMode::kKeyShare));
    ASSERT_TRUE(manager.acquire_row(k_xid, k_relation, k_rid, RowLockMode::kKeyShare));
    auto implicit_row_upgrade = manager.acquire_row(k_xid, k_relation, k_rid, RowLockMode::kShare);
    ASSERT_FALSE(implicit_row_upgrade);
    EXPECT_EQ(implicit_row_upgrade.error().code(), base::ErrorCode::kTransactionState);
    ASSERT_TRUE(manager.upgrade_row(k_xid, k_relation, k_rid, RowLockMode::kShare));
    EXPECT_EQ(manager.held_row_mode(k_xid, k_relation, k_rid), RowLockMode::kShare);
    EXPECT_EQ(
        manager.stats(),
        (LockManagerStats{
            .table_targets = 1,
            .row_targets = 1,
            .granted_locks = 2,
            .waiting_requests = 0
        })
    );

    ASSERT_TRUE(manager.release_all(k_xid));
    ASSERT_TRUE(manager.release_all(k_xid));
    EXPECT_EQ(manager.stats(), LockManagerStats{});
    auto invalid_release = manager.release_all(base::kInvalidXid);
    ASSERT_FALSE(invalid_release);
    EXPECT_EQ(invalid_release.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(LockManagerTest, ReusedQuiescentStatesDoNotRetainPriorOwnership) {
    LockManager manager;
    constexpr auto k_first_xid = base::Xid{1};
    constexpr auto k_second_xid = base::Xid{33};
    constexpr auto k_first_relation = base::RelationId{17};
    constexpr auto k_second_relation = base::RelationId{33};
    constexpr auto k_first_rid = base::Rid{base::PageId{2}, base::SlotId{1}};
    constexpr auto k_second_rid = base::Rid{base::PageId{1}, base::SlotId{2}};
    // These keys share table, row, and owner shards, forcing every cache to
    // rekey a prior node rather than allocate a fresh one.

    ASSERT_TRUE(manager.acquire_table(k_first_xid, k_first_relation, TableLockMode::kRowShare));
    ASSERT_TRUE(
        manager.acquire_row(k_first_xid, k_first_relation, k_first_rid, RowLockMode::kKeyShare)
    );
    ASSERT_TRUE(manager.release_all(k_first_xid));
    EXPECT_EQ(manager.stats(), LockManagerStats{});

    ASSERT_TRUE(manager.acquire_table(k_second_xid, k_second_relation, TableLockMode::kRowShare));
    ASSERT_TRUE(
        manager.acquire_row(k_second_xid, k_second_relation, k_second_rid, RowLockMode::kShare)
    );
    EXPECT_FALSE(manager.held_table_mode(k_first_xid, k_first_relation).has_value());
    EXPECT_FALSE(manager.held_row_mode(k_first_xid, k_first_relation, k_first_rid).has_value());
    EXPECT_EQ(manager.held_table_mode(k_second_xid, k_second_relation), TableLockMode::kRowShare);
    EXPECT_EQ(
        manager.held_row_mode(k_second_xid, k_second_relation, k_second_rid),
        RowLockMode::kShare
    );

    ASSERT_TRUE(manager.release_all(k_second_xid));
    EXPECT_EQ(manager.stats(), LockManagerStats{});
}

TEST(LockManagerTest, TableQueueIsFifoAndGrantsACompatiblePrefix) {
    LockManager manager;
    constexpr auto k_relation = base::RelationId{20};
    constexpr auto k_first = base::Xid{1};
    constexpr auto k_second = base::Xid{2};
    constexpr auto k_third = base::Xid{3};
    constexpr auto k_fourth = base::Xid{4};
    ASSERT_TRUE(manager.acquire_table(k_first, k_relation, TableLockMode::kRowExclusive));
    ASSERT_TRUE(manager.acquire_table(k_second, k_relation, TableLockMode::kRowExclusive));

    std::optional<base::Status> third_status;
    std::optional<base::Status> fourth_status;
    std::atomic<bool> third_done{false};
    std::atomic<bool> fourth_done{false};
    std::thread third{[&] {
        third_status.emplace(manager.acquire_table(k_third, k_relation, TableLockMode::kShare));
        third_done.store(true, std::memory_order_release);
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 1));
    std::thread fourth{[&] {
        fourth_status.emplace(
            manager.acquire_table(k_fourth, k_relation, TableLockMode::kAccessShare)
        );
        fourth_done.store(true, std::memory_order_release);
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 2));
    EXPECT_FALSE(third_done.load(std::memory_order_acquire));
    EXPECT_FALSE(fourth_done.load(std::memory_order_acquire));

    ASSERT_TRUE(manager.release_all(k_first));
    EXPECT_TRUE(wait_for_waiters(manager, 2));
    ASSERT_TRUE(manager.release_all(k_second));
    EXPECT_TRUE(wait_for_flag(third_done));
    EXPECT_TRUE(wait_for_flag(fourth_done));
    third.join();
    fourth.join();
    ASSERT_TRUE(third_status.has_value());
    ASSERT_TRUE(fourth_status.has_value());
    EXPECT_TRUE(*third_status);
    EXPECT_TRUE(*fourth_status);
    EXPECT_EQ(manager.held_table_mode(k_third, k_relation), TableLockMode::kShare);
    EXPECT_EQ(manager.held_table_mode(k_fourth, k_relation), TableLockMode::kAccessShare);
    ASSERT_TRUE(manager.release_all(k_third));
    ASSERT_TRUE(manager.release_all(k_fourth));
}

TEST(LockManagerTest, RejectsAnotherAcquireWhileTheSameOwnerWaits) {
    LockManager manager{LockManagerOptions{.detect_deadlocks = false}};
    constexpr auto k_waiter = base::Xid{1};
    constexpr auto k_blocker = base::Xid{2};
    constexpr auto k_blocked_relation = base::RelationId{25};
    constexpr auto k_independent_relation = base::RelationId{26};
    ASSERT_TRUE(
        manager.acquire_table(k_blocker, k_blocked_relation, TableLockMode::kAccessExclusive)
    );

    std::optional<base::Status> wait_status;
    std::thread waiter{[&] {
        wait_status.emplace(
            manager.acquire_table(k_waiter, k_blocked_relation, TableLockMode::kAccessExclusive)
        );
    }};
    ASSERT_TRUE(wait_for_waiters(manager, 1U));

    auto concurrent =
        manager.acquire_table(k_waiter, k_independent_relation, TableLockMode::kAccessShare);
    ASSERT_FALSE(concurrent);
    EXPECT_EQ(concurrent.error().code(), base::ErrorCode::kTransactionState);
    EXPECT_FALSE(manager.held_table_mode(k_waiter, k_independent_relation).has_value());

    ASSERT_TRUE(manager.release_all(k_blocker));
    waiter.join();
    ASSERT_TRUE(wait_status.has_value());
    EXPECT_TRUE(*wait_status);
    ASSERT_TRUE(manager.release_all(k_waiter));
    EXPECT_EQ(manager.stats(), LockManagerStats{});
}

TEST(LockManagerTest, RowLocksAreRelationScopedAndRespectTheirCompatibility) {
    LockManager manager;
    constexpr auto k_first_relation = base::RelationId{30};
    constexpr auto k_second_relation = base::RelationId{31};
    constexpr auto k_rid = base::Rid{base::PageId{8}, base::SlotId{2}};
    constexpr auto k_first = base::Xid{1};
    constexpr auto k_second = base::Xid{2};
    constexpr auto k_third = base::Xid{3};
    constexpr auto k_other_relation = base::Xid{4};
    for (const auto xid : {k_first, k_second, k_third}) {
        ASSERT_TRUE(manager.acquire_table(xid, k_first_relation, TableLockMode::kRowShare));
    }
    ASSERT_TRUE(
        manager.acquire_table(k_other_relation, k_second_relation, TableLockMode::kRowShare)
    );
    ASSERT_TRUE(manager.acquire_row(k_first, k_first_relation, k_rid, RowLockMode::kKeyShare));
    ASSERT_TRUE(manager.acquire_row(k_second, k_first_relation, k_rid, RowLockMode::kShare));
    ASSERT_TRUE(
        manager.acquire_row(k_other_relation, k_second_relation, k_rid, RowLockMode::kExclusive)
    );

    std::optional<base::Status> third_status;
    std::atomic<bool> third_done{false};
    std::thread third{[&] {
        third_status.emplace(
            manager.acquire_row(k_third, k_first_relation, k_rid, RowLockMode::kExclusive)
        );
        third_done.store(true, std::memory_order_release);
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 1));
    ASSERT_TRUE(manager.release_all(k_first));
    EXPECT_TRUE(wait_for_waiters(manager, 1));
    ASSERT_TRUE(manager.release_all(k_second));
    EXPECT_TRUE(wait_for_flag(third_done));
    third.join();
    ASSERT_TRUE(third_status.has_value());
    EXPECT_TRUE(*third_status);
    EXPECT_EQ(manager.held_row_mode(k_third, k_first_relation, k_rid), RowLockMode::kExclusive);
    ASSERT_TRUE(manager.release_all(k_third));
    ASSERT_TRUE(manager.release_all(k_other_relation));
    EXPECT_EQ(manager.stats(), LockManagerStats{});
}

TEST(LockManagerTest, UpgradeRetainsOldGrantAndPreventsQueueBarging) {
    LockManager manager;
    constexpr auto k_relation = base::RelationId{40};
    constexpr auto k_rid = base::Rid{base::PageId{5}, base::SlotId{1}};
    constexpr auto k_first = base::Xid{1};
    constexpr auto k_second = base::Xid{2};
    constexpr auto k_third = base::Xid{3};
    for (const auto xid : {k_first, k_second, k_third}) {
        ASSERT_TRUE(manager.acquire_table(xid, k_relation, TableLockMode::kRowShare));
    }
    ASSERT_TRUE(manager.acquire_row(k_first, k_relation, k_rid, RowLockMode::kShare));
    ASSERT_TRUE(manager.acquire_row(k_second, k_relation, k_rid, RowLockMode::kShare));

    std::optional<base::Status> upgrade_status;
    std::optional<base::Status> third_status;
    std::atomic<bool> upgrade_done{false};
    std::atomic<bool> third_done{false};
    std::thread upgrade{[&] {
        upgrade_status.emplace(
            manager.upgrade_row(k_first, k_relation, k_rid, RowLockMode::kExclusive)
        );
        upgrade_done.store(true, std::memory_order_release);
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 1));
    EXPECT_EQ(manager.held_row_mode(k_first, k_relation, k_rid), RowLockMode::kShare);
    std::thread third{[&] {
        third_status.emplace(
            manager.acquire_row(k_third, k_relation, k_rid, RowLockMode::kKeyShare)
        );
        third_done.store(true, std::memory_order_release);
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 2));
    ASSERT_TRUE(manager.release_all(k_second));
    EXPECT_TRUE(wait_for_flag(upgrade_done));
    EXPECT_FALSE(third_done.load(std::memory_order_acquire));
    upgrade.join();
    ASSERT_TRUE(upgrade_status.has_value());
    EXPECT_TRUE(*upgrade_status);
    EXPECT_EQ(manager.held_row_mode(k_first, k_relation, k_rid), RowLockMode::kExclusive);

    ASSERT_TRUE(manager.release_all(k_first));
    EXPECT_TRUE(wait_for_flag(third_done));
    third.join();
    ASSERT_TRUE(third_status.has_value());
    EXPECT_TRUE(*third_status);
    ASSERT_TRUE(manager.release_all(k_third));
}

TEST(LockManagerTest, DetectsTwoTargetDeadlockAndSelectsRequesterAsVictim) {
    LockManager manager{LockManagerOptions{.deadlock_timeout = 10ms}};
    constexpr auto k_first = base::Xid{1};
    constexpr auto k_second = base::Xid{2};
    constexpr auto k_first_relation = base::RelationId{50};
    constexpr auto k_second_relation = base::RelationId{51};
    ASSERT_TRUE(manager.acquire_table(k_first, k_first_relation, TableLockMode::kAccessExclusive));
    ASSERT_TRUE(
        manager.acquire_table(k_second, k_second_relation, TableLockMode::kAccessExclusive)
    );

    std::optional<base::Status> first_status;
    std::thread first_waiter{[&] {
        first_status.emplace(
            manager.acquire_table(k_first, k_second_relation, TableLockMode::kAccessExclusive)
        );
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 1));
    auto victim =
        manager.acquire_table(k_second, k_first_relation, TableLockMode::kAccessExclusive);
    ASSERT_FALSE(victim);
    EXPECT_EQ(victim.error().code(), base::ErrorCode::kDeadlockDetected);
    EXPECT_TRUE(victim.error().is_retryable());
    EXPECT_EQ(manager.stats().waiting_requests, 1U);

    ASSERT_TRUE(manager.release_all(k_second));
    first_waiter.join();
    ASSERT_TRUE(first_status.has_value());
    EXPECT_TRUE(*first_status);
    EXPECT_EQ(manager.held_table_mode(k_first, k_second_relation), TableLockMode::kAccessExclusive);
    ASSERT_TRUE(manager.release_all(k_first));
}

TEST(LockManagerTest, DetectsMixedRowAndTransactionWaitCycleInOneGraph) {
    TransactionManager transactions{LockManagerOptions{.deadlock_timeout = 10ms}};
    auto first_lease = transactions.register_session();
    auto second_lease = transactions.register_session();
    ASSERT_TRUE(first_lease);
    ASSERT_TRUE(second_lease);
    auto first_xid = transactions.assign_xid(*first_lease);
    auto second_xid = transactions.assign_xid(*second_lease);
    ASSERT_TRUE(first_xid);
    ASSERT_TRUE(second_xid);
    const auto k_first = *first_xid;
    const auto k_second = *second_xid;
    auto& manager = transactions.lock_manager();
    constexpr auto k_relation = base::RelationId{55};
    constexpr auto k_rid = base::Rid{base::PageId{4}, base::SlotId{2}};
    ASSERT_TRUE(manager.acquire_table(k_first, k_relation, TableLockMode::kRowShare));
    ASSERT_TRUE(manager.acquire_table(k_second, k_relation, TableLockMode::kRowShare));
    ASSERT_TRUE(manager.acquire_row(k_first, k_relation, k_rid, RowLockMode::kExclusive));

    std::optional<base::Status> second_status;
    std::thread second_waiter{[&] {
        second_status.emplace(
            manager.acquire_row(k_second, k_relation, k_rid, RowLockMode::kExclusive)
        );
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 1U));

    auto victim = transactions.xid_waiter().wait_for(k_second, k_first);
    ASSERT_FALSE(victim);
    EXPECT_EQ(victim.error().code(), base::ErrorCode::kDeadlockDetected);
    EXPECT_TRUE(victim.error().is_retryable());
    EXPECT_EQ(manager.stats().waiting_requests, 1U);

    ASSERT_TRUE(transactions.complete(*first_lease, k_first, TransactionStatus::kAborted));
    second_waiter.join();
    ASSERT_TRUE(second_status.has_value());
    EXPECT_TRUE(*second_status);
    EXPECT_EQ(manager.held_row_mode(k_second, k_relation, k_rid), RowLockMode::kExclusive);
    ASSERT_TRUE(transactions.complete(*second_lease, k_second, TransactionStatus::kAborted));
    EXPECT_EQ(manager.stats(), LockManagerStats{});
    ASSERT_TRUE(first_lease->close());
    ASSERT_TRUE(second_lease->close());
}

TEST(LockManagerTest, DetectsCompetingUpgradeDeadlockWithoutDroppingOldGrant) {
    LockManager manager{LockManagerOptions{.deadlock_timeout = 10ms}};
    constexpr auto k_relation = base::RelationId{60};
    constexpr auto k_first = base::Xid{1};
    constexpr auto k_second = base::Xid{2};
    ASSERT_TRUE(manager.acquire_table(k_first, k_relation, TableLockMode::kRowShare));
    ASSERT_TRUE(manager.acquire_table(k_second, k_relation, TableLockMode::kRowShare));

    std::optional<base::Status> first_status;
    std::thread first_upgrade{[&] {
        first_status.emplace(
            manager.upgrade_table(k_first, k_relation, TableLockMode::kAccessExclusive)
        );
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 1));
    auto victim = manager.upgrade_table(k_second, k_relation, TableLockMode::kAccessExclusive);
    ASSERT_FALSE(victim);
    EXPECT_EQ(victim.error().code(), base::ErrorCode::kDeadlockDetected);
    EXPECT_EQ(manager.held_table_mode(k_second, k_relation), TableLockMode::kRowShare);

    ASSERT_TRUE(manager.release_all(k_second));
    first_upgrade.join();
    ASSERT_TRUE(first_status.has_value());
    EXPECT_TRUE(*first_status);
    EXPECT_EQ(manager.held_table_mode(k_first, k_relation), TableLockMode::kAccessExclusive);
    ASSERT_TRUE(manager.release_all(k_first));
}

TEST(LockManagerTest, DisabledDetectorAllowsExplicitWaitCancellation) {
    LockManager manager{LockManagerOptions{.detect_deadlocks = false}};
    constexpr auto k_first = base::Xid{1};
    constexpr auto k_second = base::Xid{2};
    constexpr auto k_first_relation = base::RelationId{70};
    constexpr auto k_second_relation = base::RelationId{71};
    ASSERT_TRUE(manager.acquire_table(k_first, k_first_relation, TableLockMode::kAccessExclusive));
    ASSERT_TRUE(
        manager.acquire_table(k_second, k_second_relation, TableLockMode::kAccessExclusive)
    );

    std::optional<base::Status> first_status;
    std::optional<base::Status> second_status;
    std::thread first_waiter{[&] {
        first_status.emplace(
            manager.acquire_table(k_first, k_second_relation, TableLockMode::kAccessExclusive)
        );
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 1));
    std::thread second_waiter{[&] {
        second_status.emplace(
            manager.acquire_table(k_second, k_first_relation, TableLockMode::kAccessExclusive)
        );
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 2));
    ASSERT_TRUE(manager.cancel_wait(k_second));
    second_waiter.join();
    ASSERT_TRUE(second_status.has_value());
    ASSERT_FALSE(*second_status);
    EXPECT_EQ(second_status->error().code(), base::ErrorCode::kCancelled);

    ASSERT_TRUE(manager.release_all(k_second));
    first_waiter.join();
    ASSERT_TRUE(first_status.has_value());
    EXPECT_TRUE(*first_status);
    ASSERT_TRUE(manager.release_all(k_first));
    EXPECT_EQ(manager.stats(), LockManagerStats{});
}

TEST(LockManagerTest, CancellationInterruptsDeadlockTimeoutSleep) {
    LockManager manager{LockManagerOptions{.deadlock_timeout = 5s}};
    constexpr auto k_relation = base::RelationId{75};
    constexpr auto k_holder = base::Xid{1};
    constexpr auto k_waiter = base::Xid{2};
    ASSERT_TRUE(manager.acquire_table(k_holder, k_relation, TableLockMode::kAccessExclusive));

    std::optional<base::Status> waiter_status;
    std::thread waiter{[&] {
        waiter_status.emplace(
            manager.acquire_table(k_waiter, k_relation, TableLockMode::kAccessShare)
        );
    }};
    EXPECT_TRUE(wait_for_waiters(manager, 1));
    ASSERT_TRUE(manager.cancel_wait(k_waiter));
    waiter.join();

    ASSERT_TRUE(waiter_status.has_value());
    ASSERT_FALSE(*waiter_status);
    EXPECT_EQ(waiter_status->error().code(), base::ErrorCode::kCancelled);
    ASSERT_TRUE(manager.release_all(k_holder));
    EXPECT_EQ(manager.stats(), LockManagerStats{});
}

TEST(LockManagerTest, ConcurrentRowContentionPreservesMutualExclusionAndCleansUp) {
    constexpr std::size_t k_workers = 8;
    constexpr std::size_t k_rounds = 80;
    constexpr std::size_t k_rows = 4;
    constexpr auto k_relation = base::RelationId{80};
    LockManager manager;
    std::array<std::atomic<std::size_t>, k_rows> active{};
    std::atomic<std::size_t> completed{0};
    std::atomic<bool> succeeded{true};
    std::vector<std::thread> workers;
    workers.reserve(k_workers);
    for (std::size_t worker = 0; worker < k_workers; ++worker) {
        workers.emplace_back([&, worker] {
            const auto xid = base::Xid{static_cast<std::uint32_t>(worker + 1U)};
            for (std::size_t round = 0; round < k_rounds; ++round) {
                const auto row = (worker + round) % k_rows;
                const auto rid =
                    base::Rid{base::PageId{1}, base::SlotId{static_cast<std::uint16_t>(row)}};
                if (!manager.acquire_table(xid, k_relation, TableLockMode::kRowExclusive)
                    || !manager.acquire_row(xid, k_relation, rid, RowLockMode::kExclusive)) {
                    succeeded.store(false, std::memory_order_relaxed);
                    static_cast<void>(manager.release_all(xid));
                    return;
                }
                if (active.at(row).fetch_add(1, std::memory_order_acq_rel) != 0) {
                    succeeded.store(false, std::memory_order_relaxed);
                }
                std::this_thread::yield();
                active.at(row).fetch_sub(1, std::memory_order_acq_rel);
                completed.fetch_add(1, std::memory_order_relaxed);
                if (!manager.release_all(xid)) {
                    succeeded.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    EXPECT_TRUE(succeeded.load(std::memory_order_relaxed));
    EXPECT_EQ(completed.load(std::memory_order_relaxed), k_workers * k_rounds);
    for (const auto& count : active) {
        EXPECT_EQ(count.load(std::memory_order_relaxed), 0U);
    }
    EXPECT_EQ(manager.stats(), LockManagerStats{});
}

TEST(LockManagerTest, ConcurrentIndependentRelationsKeepOwnershipAndTargetsConsistent) {
    constexpr std::size_t k_workers = 16;
    constexpr std::size_t k_rounds = 100;
    LockManager manager;
    std::atomic<std::size_t> completed{0};
    std::atomic<bool> succeeded{true};
    std::vector<std::thread> workers;
    workers.reserve(k_workers);
    for (std::size_t worker = 0; worker < k_workers; ++worker) {
        workers.emplace_back([&, worker] {
            const auto xid = base::Xid{static_cast<std::uint32_t>(worker + 1U)};
            const auto relation = base::RelationId{static_cast<std::uint32_t>(worker + 100U)};
            const auto rid = base::Rid{base::PageId{1U}, base::SlotId{1U}};
            for (std::size_t round = 0; round < k_rounds; ++round) {
                if (!manager.acquire_table(xid, relation, TableLockMode::kRowShare)
                    || !manager.acquire_row(xid, relation, rid, RowLockMode::kExclusive)
                    || !manager.release_all(xid)) {
                    succeeded.store(false, std::memory_order_relaxed);
                    static_cast<void>(manager.release_all(xid));
                    return;
                }
                completed.fetch_add(1U, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    EXPECT_TRUE(succeeded.load(std::memory_order_relaxed));
    EXPECT_EQ(completed.load(std::memory_order_relaxed), k_workers * k_rounds);
    EXPECT_EQ(manager.stats(), LockManagerStats{});
}

}  // namespace
}  // namespace db::transaction
