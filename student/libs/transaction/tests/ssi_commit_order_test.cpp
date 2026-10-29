#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"

#include "ssi_commit_order.hpp"
#include "ssi_lifecycle.hpp"
#include "ssi_state.hpp"

namespace db::transaction::detail {
namespace {

TEST(SsiCommitOrderTest, SequenceOverflowRemainsStructured) {
    auto sequence = std::numeric_limits<std::uint64_t>::max();
    auto advanced = advance_ssi_commit_sequence(sequence);
    ASSERT_FALSE(advanced);
    EXPECT_EQ(advanced.error().code(), base::ErrorCode::kResourceExhausted);
}

TEST(SsiCommitOrderTest, TailPreparationDoomsActiveDangerousPivot) {
    constexpr auto k_head = base::Xid{100};
    constexpr auto k_pivot = base::Xid{200};
    constexpr auto k_tail = base::Xid{300};
    SsiStateMap states;
    for (const auto xid : {k_head, k_pivot, k_tail}) {
        states.emplace(xid, SsiTransactionState{1}).first->second.snapshot_set = true;
    }
    states.at(k_head).outgoing.push_back(k_pivot);
    states.at(k_pivot).incoming.push_back(k_head);
    states.at(k_pivot).outgoing.push_back(k_tail);
    states.at(k_tail).incoming.push_back(k_pivot);
    ActiveSnapshotHorizon active_snapshots;
    add_active_ssi_snapshot(active_snapshots, 1U);
    add_active_ssi_snapshot(active_snapshots, 1U);
    add_active_ssi_snapshot(active_snapshots, 1U);
    auto sequence = std::uint64_t{1};

    ASSERT_TRUE(prepare_ssi_transaction(states, active_snapshots, sequence, k_tail));

    EXPECT_TRUE(states.at(k_pivot).doomed);
    EXPECT_EQ(states.at(k_tail).prepare_sequence, 2U);
}

}  // namespace
}  // namespace db::transaction::detail
