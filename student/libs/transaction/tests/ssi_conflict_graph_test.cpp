#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/transaction/proc_array.hpp"

#include "ssi_conflict_graph.hpp"
#include "ssi_lifecycle.hpp"
#include "ssi_state.hpp"

namespace db::transaction::detail {
namespace {

TEST(SsiConflictGraphTest, EdgeInsertionIsSymmetricDeduplicatedAndDoomsCycleActor) {
    constexpr auto k_first = base::Xid{100};
    constexpr auto k_second = base::Xid{200};
    SsiStateMap states;
    states.emplace(k_first, SsiTransactionState{1});
    states.emplace(k_second, SsiTransactionState{1});
    ActiveSnapshotHorizon active_snapshots;
    add_active_ssi_snapshot(active_snapshots, 1U);
    add_active_ssi_snapshot(active_snapshots, 1U);

    flag_ssi_rw_conflict(states, active_snapshots, k_second, k_first, k_first);
    flag_ssi_rw_conflict(states, active_snapshots, k_second, k_first, k_first);
    ASSERT_EQ(states.at(k_second).outgoing, std::vector<base::Xid>({k_first}));
    ASSERT_EQ(states.at(k_first).incoming, std::vector<base::Xid>({k_second}));

    flag_ssi_rw_conflict(states, active_snapshots, k_first, k_second, k_second);
    EXPECT_TRUE(states.at(k_second).doomed);
    EXPECT_EQ(states.at(k_first).outgoing, std::vector<base::Xid>({k_second}));
}

TEST(SsiConflictGraphTest, RetainedReaderUsesWriterMvccSnapshotForOverlap) {
    constexpr auto k_reader = base::Xid{100};
    SsiTransactionState reader{1};
    reader.prepare_sequence = 2;
    reader.commit_sequence = 3;
    SsiTransactionState writer{3};
    writer.snapshot_set = true;
    writer.snapshot = Snapshot{
        .xmin = k_reader,
        .xmax = base::Xid{201},
        .in_progress = {k_reader},
        .command_id = 0
    };

    EXPECT_TRUE(ssi_reader_can_conflict_with_writer(k_reader, reader, writer));
    writer.snapshot = Snapshot{
        .xmin = base::Xid{200},
        .xmax = base::Xid{201},
        .in_progress = {},
        .command_id = 0
    };
    EXPECT_FALSE(ssi_reader_can_conflict_with_writer(k_reader, reader, writer));
}

}  // namespace
}  // namespace db::transaction::detail
