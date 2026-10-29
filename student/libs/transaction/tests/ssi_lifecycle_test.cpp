#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/transaction/ssi.hpp"

#include "ssi_lifecycle.hpp"
#include "ssi_read_locks.hpp"
#include "ssi_state.hpp"

namespace db::transaction::detail {
namespace {

TEST(SsiLifecycleTest, RemovingCommittedEndpointPreservesSummaryAndRebuildsIndex) {
    constexpr auto k_owner = base::Xid{100};
    constexpr auto k_removed = base::Xid{200};
    const auto owner_target = SsiReadTarget::for_relation(base::RelationId{1});
    const auto removed_target = SsiReadTarget::for_relation(base::RelationId{2});
    SsiStateMap states;
    auto& owner = states.emplace(k_owner, SsiTransactionState{1}).first->second;
    owner.outgoing.push_back(k_removed);
    owner.read_locks.push_back(SsiReadRecord{.target = owner_target});
    auto& removed = states.emplace(k_removed, SsiTransactionState{1}).first->second;
    removed.incoming.push_back(k_owner);
    removed.prepare_sequence = 4;
    removed.commit_sequence = 5;
    removed.read_locks.push_back(SsiReadRecord{.target = removed_target});
    removed.predicate_locks.push_back(
        SsiPredicateLock{
            .id = 1,
            .relation_id = base::RelationId{2},
            .predicate = SsiPredicate::all(),
        }
    );
    SsiTargetIndex index;
    SsiPredicateIndex predicate_index;
    predicate_index[base::RelationId{2}].push_back(k_removed);
    rebuild_read_lock_index(states, index);
    ActiveSnapshotHorizon active_snapshots;
    add_active_ssi_snapshot(active_snapshots, 1U);
    CommittedStateIndex committed_states{{5U, k_removed}};

    remove_ssi_transactions(
        states,
        index,
        predicate_index,
        active_snapshots,
        committed_states,
        {k_removed}
    );

    ASSERT_FALSE(states.contains(k_removed));
    EXPECT_TRUE(owner.outgoing.empty());
    EXPECT_EQ(owner.earliest_outgoing_commit, 4U);
    EXPECT_TRUE(index.contains(owner_target));
    EXPECT_FALSE(index.contains(removed_target));
    EXPECT_FALSE(predicate_index.contains(base::RelationId{2}));
}

TEST(SsiLifecycleTest, PruningWaitsForTheOldestActiveSnapshot) {
    constexpr auto k_active = base::Xid{100};
    constexpr auto k_old_commit = base::Xid{200};
    constexpr auto k_new_commit = base::Xid{300};
    SsiStateMap states;
    states.emplace(k_active, SsiTransactionState{5});
    auto& old_commit = states.emplace(k_old_commit, SsiTransactionState{1}).first->second;
    old_commit.prepare_sequence = 3;
    old_commit.commit_sequence = 4;
    auto& new_commit = states.emplace(k_new_commit, SsiTransactionState{1}).first->second;
    new_commit.prepare_sequence = 6;
    new_commit.commit_sequence = 7;
    SsiTargetIndex index;
    SsiPredicateIndex predicate_index;
    ActiveSnapshotHorizon active_snapshots;
    add_active_ssi_snapshot(active_snapshots, 5U);
    CommittedStateIndex committed_states{{4U, k_old_commit}, {7U, k_new_commit}};

    prune_ssi_committed_states(states, index, predicate_index, active_snapshots, committed_states);

    EXPECT_FALSE(states.contains(k_old_commit));
    EXPECT_TRUE(states.contains(k_new_commit));
}

}  // namespace
}  // namespace db::transaction::detail
