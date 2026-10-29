#include <algorithm>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/transaction/proc_array.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction {
namespace {

auto register_session(ProcArray& proc_array) -> ProcSlotLease {
    auto registered = proc_array.register_session();
    EXPECT_TRUE(registered);
    return std::move(*registered);
}

auto assign_xid(ProcArray& proc_array, ProcSlotLease& lease) -> base::Xid {
    auto assigned = proc_array.assign_xid(lease);
    EXPECT_TRUE(assigned);
    return *assigned;
}

TEST(ProcArrayTest, ValidatesAndRestoresDurableHorizon) {
    auto invalid_next = ProcArray::from_horizon(
        XidHorizon{.next_xid = base::kInvalidXid, .latest_completed_xid = base::kInvalidXid}
    );
    ASSERT_FALSE(invalid_next);
    EXPECT_EQ(invalid_next.error().code(), base::ErrorCode::kInvalidArgument);

    auto invalid_completed = ProcArray::from_horizon(
        XidHorizon{.next_xid = base::Xid{7}, .latest_completed_xid = base::Xid{7}}
    );
    ASSERT_FALSE(invalid_completed);
    EXPECT_EQ(invalid_completed.error().code(), base::ErrorCode::kInvalidArgument);

    auto boundary_completed = ProcArray::from_horizon(
        XidHorizon{.next_xid = kXidWrapBoundary, .latest_completed_xid = kXidWrapBoundary}
    );
    ASSERT_FALSE(boundary_completed);
    EXPECT_EQ(boundary_completed.error().code(), base::ErrorCode::kInvalidArgument);

    auto restored = ProcArray::from_horizon(
        XidHorizon{.next_xid = base::Xid{41}, .latest_completed_xid = base::Xid{39}}
    );
    ASSERT_TRUE(restored);
    EXPECT_EQ(
        (*restored)->horizon(),
        (XidHorizon{.next_xid = base::Xid{41}, .latest_completed_xid = base::Xid{39}})
    );
    auto lease = register_session(**restored);
    EXPECT_EQ(assign_xid(**restored, lease), base::Xid{41});
    ASSERT_TRUE((*restored)->publish_completion(lease, base::Xid{41}));
    EXPECT_EQ(
        (*restored)->horizon(),
        (XidHorizon{.next_xid = base::Xid{42}, .latest_completed_xid = base::Xid{41}})
    );
    EXPECT_TRUE(lease.close());

    auto exhausted = ProcArray::from_horizon(
        XidHorizon{.next_xid = kXidWrapBoundary, .latest_completed_xid = base::Xid{UINT32_MAX - 1U}}
    );
    ASSERT_TRUE(exhausted);
    auto exhausted_lease = register_session(**exhausted);
    auto allocation = (*exhausted)->assign_xid(exhausted_lease);
    ASSERT_FALSE(allocation);
    EXPECT_EQ(allocation.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_EQ((*exhausted)->horizon().next_xid, kXidWrapBoundary);
    EXPECT_TRUE(exhausted_lease.close());
}

TEST(ProcArrayTest, DenseRegistrationReusesStableIdleSlots) {
    ProcArray proc_array;
    auto first = register_session(proc_array);
    auto second = register_session(proc_array);
    const auto* const first_address = first.slot();
    ASSERT_NE(first_address, nullptr);
    const auto first_id = first.slot()->slot_id();
    EXPECT_EQ(proc_array.registered_session_count(), 2U);
    EXPECT_EQ(proc_array.slot_capacity(), 2U);

    ASSERT_TRUE(first.close());
    EXPECT_EQ(proc_array.registered_session_count(), 1U);
    auto replacement = register_session(proc_array);
    EXPECT_EQ(replacement.slot(), first_address);
    EXPECT_EQ(replacement.slot()->slot_id(), first_id);
    EXPECT_EQ(proc_array.registered_session_count(), 2U);
    EXPECT_EQ(proc_array.slot_capacity(), 2U);

    ASSERT_TRUE(second.close());
    ASSERT_TRUE(replacement.close());
    EXPECT_EQ(proc_array.registered_session_count(), 0U);
    auto duplicate_close = replacement.close();
    ASSERT_FALSE(duplicate_close);
    EXPECT_EQ(duplicate_close.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(ProcArrayTest, LeaseObserverIsNullAfterMoveOrClose) {
    ProcArray proc_array;
    auto source = register_session(proc_array);
    const auto* const stable_slot = source.slot();
    ASSERT_NE(stable_slot, nullptr);

    auto lease = std::move(source);
    // Deliberately verifies ProcSlotLease's documented moved-from observer contract.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_EQ(source.slot(), nullptr);
    EXPECT_EQ(lease.slot(), stable_slot);

    ASSERT_TRUE(lease.close());
    EXPECT_FALSE(lease.valid());
    EXPECT_EQ(lease.slot(), nullptr);
}

TEST(ProcArrayTest, LiveTransactionCannotDisappearWhenSessionDisconnects) {
    ProcArray proc_array;
    auto lease = register_session(proc_array);
    const auto* const stable_address = lease.slot();
    ASSERT_NE(stable_address, nullptr);
    const auto xid = assign_xid(proc_array, lease);
    EXPECT_TRUE(proc_array.has_at_least_active_transactions(1));

    auto premature_close = lease.close();
    ASSERT_FALSE(premature_close);
    EXPECT_EQ(premature_close.error().code(), base::ErrorCode::kTransactionState);
    EXPECT_TRUE(lease.valid());
    EXPECT_EQ(proc_array.registered_session_count(), 1U);
    EXPECT_EQ(lease.slot()->xid(), xid);

    ASSERT_TRUE(proc_array.publish_completion(lease, xid));
    EXPECT_FALSE(proc_array.has_at_least_active_transactions(1));
    EXPECT_EQ(lease.slot(), stable_address);
    const auto next_xid = assign_xid(proc_array, lease);
    EXPECT_EQ(next_xid, base::Xid{xid.value() + 1U});
    ASSERT_TRUE(proc_array.publish_completion(lease, next_xid));
    ASSERT_TRUE(lease.close());
}

TEST(ProcArrayTest, IdleLeaseCanOutliveAndReleaseItsProcArrayFacade) {
    auto proc_array = std::make_unique<ProcArray>();
    auto registered = proc_array->register_session();
    ASSERT_TRUE(registered);
    auto lease = std::move(*registered);
    ASSERT_NE(lease.slot(), nullptr);
    const auto slot_id = lease.slot()->slot_id();

    proc_array.reset();
    EXPECT_TRUE(lease.valid());
    EXPECT_EQ(lease.slot()->slot_id(), slot_id);
    EXPECT_TRUE(lease.close());
    EXPECT_FALSE(lease.valid());
}

TEST(ProcArrayTest, SnapshotUsesLatestCompletedBoundaryAndDenseActiveXids) {
    ProcArray proc_array;
    auto first = register_session(proc_array);
    auto second = register_session(proc_array);
    const auto first_xid = assign_xid(proc_array, first);
    const auto second_xid = assign_xid(proc_array, second);

    auto initial = proc_array.snapshot(first_xid);
    ASSERT_TRUE(initial);
    EXPECT_EQ(initial->xmin, first_xid);
    EXPECT_EQ(initial->xmax, kFirstNormalXid);
    EXPECT_TRUE(initial->in_progress.empty());

    ASSERT_TRUE(proc_array.publish_completion(second, second_xid));
    auto third = register_session(proc_array);
    const auto third_xid = assign_xid(proc_array, third);
    ASSERT_EQ(third_xid, base::Xid{3});

    auto captured = proc_array.snapshot();
    ASSERT_TRUE(captured);
    EXPECT_EQ(captured->xmin, first_xid);
    EXPECT_EQ(captured->xmax, third_xid);
    ASSERT_EQ(captured->in_progress, std::vector<base::Xid>({first_xid}));
    EXPECT_TRUE(captured->contains(first_xid));
    EXPECT_FALSE(captured->contains(third_xid));

    ASSERT_TRUE(proc_array.publish_completion(first, first_xid));
    ASSERT_TRUE(proc_array.publish_completion(third, third_xid));
    ASSERT_TRUE(first.close());
    ASSERT_TRUE(second.close());
    ASSERT_TRUE(third.close());
}

TEST(ProcArrayTest, SnapshotCacheSurvivesAssignmentsButNotCompletions) {
    ProcArray proc_array;
    auto owner = register_session(proc_array);
    const auto owner_xid = assign_xid(proc_array, owner);
    auto first_capture = proc_array.snapshot_and_register(owner);
    ASSERT_TRUE(first_capture);
    EXPECT_FALSE(first_capture->reused);
    EXPECT_EQ(owner.slot()->snapshot_xmin(), owner_xid);
    EXPECT_EQ(first_capture->registered_snapshot.prune_xmin, owner_xid);

    auto other = register_session(proc_array);
    const auto other_xid = assign_xid(proc_array, other);
    auto reused = proc_array.snapshot_and_register(owner, &first_capture->registered_snapshot);
    ASSERT_TRUE(reused);
    EXPECT_TRUE(reused->reused);
    EXPECT_EQ(reused->registered_snapshot, first_capture->registered_snapshot);

    ASSERT_TRUE(proc_array.publish_completion(other, other_xid));
    auto rebuilt = proc_array.snapshot_and_register(owner, &reused->registered_snapshot);
    ASSERT_TRUE(rebuilt);
    EXPECT_FALSE(rebuilt->reused);
    EXPECT_GT(
        rebuilt->registered_snapshot.completion_count,
        reused->registered_snapshot.completion_count
    );
    EXPECT_EQ(rebuilt->registered_snapshot.snapshot.xmax, base::Xid{other_xid.value() + 1U});
    EXPECT_EQ(proc_array.oldest_active_xmin(), owner_xid);

    ASSERT_TRUE(proc_array.clear_registered_snapshot(owner, owner_xid));
    EXPECT_EQ(owner.slot()->snapshot_xmin(), base::kInvalidXid);
    ASSERT_TRUE(proc_array.publish_completion(owner, owner_xid));
    ASSERT_TRUE(owner.close());
    ASSERT_TRUE(other.close());
    EXPECT_EQ(proc_array.oldest_active_xmin(), kXidWrapBoundary);
}

TEST(ProcArrayTest, RejectsForeignLeaseDuplicateAssignmentAndWrongCompletion) {
    ProcArray owner_array;
    ProcArray foreign_array;
    auto lease = register_session(owner_array);

    EXPECT_TRUE(owner_array.has_at_least_active_transactions(0));
    EXPECT_FALSE(owner_array.has_at_least_active_transactions(1));
    auto invalid_snapshot = owner_array.snapshot(kXidWrapBoundary);
    ASSERT_FALSE(invalid_snapshot);
    EXPECT_EQ(invalid_snapshot.error().code(), base::ErrorCode::kInvalidArgument);
    auto idle_registration = owner_array.snapshot_and_register(lease);
    ASSERT_FALSE(idle_registration);
    EXPECT_EQ(idle_registration.error().code(), base::ErrorCode::kTransactionState);
    auto invalid_snapshot_clear = owner_array.clear_registered_snapshot(lease, base::kInvalidXid);
    ASSERT_FALSE(invalid_snapshot_clear);
    EXPECT_EQ(invalid_snapshot_clear.error().code(), base::ErrorCode::kInvalidArgument);
    auto idle_snapshot_clear = owner_array.clear_registered_snapshot(lease, base::Xid{1});
    ASSERT_FALSE(idle_snapshot_clear);
    EXPECT_EQ(idle_snapshot_clear.error().code(), base::ErrorCode::kTransactionState);
    auto invalid_completion = owner_array.publish_completion(lease, base::kInvalidXid);
    ASSERT_FALSE(invalid_completion);
    EXPECT_EQ(invalid_completion.error().code(), base::ErrorCode::kInvalidArgument);
    auto idle_completion = owner_array.publish_completion(lease, base::Xid{1});
    ASSERT_FALSE(idle_completion);
    EXPECT_EQ(idle_completion.error().code(), base::ErrorCode::kTransactionState);

    auto foreign_assign = foreign_array.assign_xid(lease);
    ASSERT_FALSE(foreign_assign);
    EXPECT_EQ(foreign_assign.error().code(), base::ErrorCode::kInvalidArgument);
    const auto xid = assign_xid(owner_array, lease);
    auto duplicate_assign = owner_array.assign_xid(lease);
    ASSERT_FALSE(duplicate_assign);
    EXPECT_EQ(duplicate_assign.error().code(), base::ErrorCode::kTransactionState);
    EXPECT_TRUE(owner_array.has_at_least_active_transactions(1));
    EXPECT_FALSE(owner_array.has_at_least_active_transactions(2));

    auto wrong_snapshot_clear =
        owner_array.clear_registered_snapshot(lease, base::Xid{xid.value() + 1U});
    ASSERT_FALSE(wrong_snapshot_clear);
    EXPECT_EQ(wrong_snapshot_clear.error().code(), base::ErrorCode::kTransactionState);

    auto wrong_completion = owner_array.publish_completion(lease, base::Xid{xid.value() + 1U});
    ASSERT_FALSE(wrong_completion);
    EXPECT_EQ(wrong_completion.error().code(), base::ErrorCode::kInvariantViolation);
    EXPECT_EQ(lease.slot()->xid(), xid);
    ASSERT_TRUE(owner_array.publish_completion(lease, xid));
    ASSERT_TRUE(lease.close());
    auto closed_assign = owner_array.assign_xid(lease);
    ASSERT_FALSE(closed_assign);
    EXPECT_EQ(closed_assign.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(ProcArrayTest, ContendedCompletionsUseLeaderGroupClearAndInvalidateSnapshots) {
    constexpr std::size_t k_threads = 16;
    ProcArray proc_array;
    auto snapshot_owner = register_session(proc_array);
    const auto snapshot_xid = assign_xid(proc_array, snapshot_owner);
    auto original = proc_array.snapshot_and_register(snapshot_owner);
    ASSERT_TRUE(original);

    std::vector<ProcSlotLease> leases;
    leases.reserve(k_threads);
    for (std::size_t index = 0; index < k_threads; ++index) {
        leases.push_back(register_session(proc_array));
    }

    std::atomic<bool> succeeded{true};
    for (std::size_t round = 0; round < 20; ++round) {
        std::vector<base::Xid> xids;
        xids.reserve(k_threads);
        for (auto& lease : leases) {
            xids.push_back(assign_xid(proc_array, lease));
        }
        std::barrier ready{static_cast<std::ptrdiff_t>(k_threads + 1U)};
        std::vector<std::thread> workers;
        workers.reserve(k_threads);
        for (std::size_t index = 0; index < k_threads; ++index) {
            workers.emplace_back([&, index] {
                ready.arrive_and_wait();
                if (!proc_array.publish_completion(leases[index], xids[index])) {
                    succeeded.store(false, std::memory_order_relaxed);
                }
            });
        }
        ready.arrive_and_wait();
        for (auto& worker : workers) {
            worker.join();
        }
        ASSERT_TRUE(succeeded.load(std::memory_order_relaxed));
        if (const auto stats = proc_array.completion_group_stats(); stats.members > stats.leaders) {
            break;
        }
    }

    const auto stats = proc_array.completion_group_stats();
    EXPECT_GT(stats.leaders, 0U);
    EXPECT_GT(stats.members, stats.leaders);
    auto refreshed =
        proc_array.snapshot_and_register(snapshot_owner, &original->registered_snapshot);
    ASSERT_TRUE(refreshed);
    EXPECT_FALSE(refreshed->reused);
    EXPECT_GT(
        refreshed->registered_snapshot.completion_count,
        original->registered_snapshot.completion_count
    );
    for (auto& lease : leases) {
        EXPECT_FALSE(lease.slot()->has_active_transaction());
        ASSERT_TRUE(lease.close());
    }
    ASSERT_TRUE(proc_array.publish_completion(snapshot_owner, snapshot_xid));
    ASSERT_TRUE(snapshot_owner.close());
}

TEST(ProcArrayTest, ConcurrentSessionsAllocateSnapshotCompleteAndReuseSafely) {
    constexpr std::size_t k_threads = 8;
    constexpr std::size_t k_transactions_per_thread = 50;
    ProcArray proc_array;
    std::atomic<bool> succeeded{true};
    std::mutex xid_mutex;
    std::vector<std::uint32_t> xids;
    xids.reserve(k_threads * k_transactions_per_thread);
    std::vector<std::thread> threads;
    threads.reserve(k_threads);

    for (std::size_t thread_index = 0; thread_index < k_threads; ++thread_index) {
        threads.emplace_back([&] {
            for (std::size_t transaction = 0; transaction < k_transactions_per_thread;
                 ++transaction) {
                auto registered = proc_array.register_session();
                if (!registered) {
                    succeeded.store(false, std::memory_order_relaxed);
                    return;
                }
                auto lease = std::move(*registered);
                auto assigned = proc_array.assign_xid(lease);
                if (!assigned) {
                    succeeded.store(false, std::memory_order_relaxed);
                    return;
                }
                const auto xid = *assigned;
                auto captured = proc_array.snapshot_and_register(lease);
                if (!captured || !proc_array.publish_completion(lease, xid) || !lease.close()) {
                    succeeded.store(false, std::memory_order_relaxed);
                    return;
                }
                const std::lock_guard result_lock{xid_mutex};
                xids.push_back(xid.value());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    ASSERT_TRUE(succeeded.load(std::memory_order_relaxed));
    std::ranges::sort(xids);
    ASSERT_EQ(xids.size(), k_threads * k_transactions_per_thread);
    for (std::size_t index = 0; index < xids.size(); ++index) {
        EXPECT_EQ(xids[index], index + 1U);
    }
    EXPECT_EQ(proc_array.registered_session_count(), 0U);
    EXPECT_LE(proc_array.slot_capacity(), k_threads);
    EXPECT_EQ(proc_array.oldest_active_xmin(), kXidWrapBoundary);
    EXPECT_EQ(
        proc_array.horizon().latest_completed_xid,
        base::Xid{static_cast<std::uint32_t>(xids.size())}
    );
}

}  // namespace
}  // namespace db::transaction
