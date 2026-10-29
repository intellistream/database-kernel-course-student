#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/transaction/ssi.hpp"

#include "ssi_predicate_locks.hpp"
#include "ssi_state.hpp"

namespace db::transaction::detail {
namespace {

TEST(SsiPredicateLocksTest, CollectionRequiresSnapshotAndMarksWriter) {
    constexpr auto k_writer = base::Xid{200};
    SsiStateMap states;
    SsiPredicateIndex predicate_index;
    states.emplace(k_writer, SsiTransactionState{1});

    auto missing_snapshot =
        collect_ssi_predicate_candidates(states, predicate_index, k_writer, base::RelationId{1});
    ASSERT_FALSE(missing_snapshot);
    EXPECT_EQ(missing_snapshot.error().code(), base::ErrorCode::kTransactionState);

    states.at(k_writer).snapshot_set = true;
    auto candidates =
        collect_ssi_predicate_candidates(states, predicate_index, k_writer, base::RelationId{1});
    ASSERT_TRUE(candidates);
    EXPECT_TRUE(candidates->empty());
    EXPECT_TRUE(states.at(k_writer).did_write);
}

TEST(SsiPredicateLocksTest, RevalidationAcceptsPromotionButRejectsRemovedCoverage) {
    constexpr auto k_reader = base::Xid{100};
    constexpr auto k_writer = base::Xid{200};
    constexpr auto k_relation = base::RelationId{1};
    SsiStateMap states;
    SsiPredicateIndex predicate_index;
    auto& reader = states.emplace(k_reader, SsiTransactionState{1}).first->second;
    reader.predicate_locks.push_back(
        SsiPredicateLock{
            .id = 7,
            .relation_id = k_relation,
            .predicate = SsiPredicate::all(),
        }
    );
    predicate_index[k_relation].push_back(k_reader);
    auto& writer = states.emplace(k_writer, SsiTransactionState{1}).first->second;
    writer.snapshot_set = true;

    auto candidates =
        collect_ssi_predicate_candidates(states, predicate_index, k_writer, k_relation);
    ASSERT_TRUE(candidates);
    ASSERT_EQ(candidates->size(), 1U);

    reader.predicate_locks.clear();
    EXPECT_TRUE(revalidate_ssi_predicate_readers(states, k_writer, *candidates).empty());
    reader.predicate_locks.push_back(
        SsiPredicateLock{
            .id = 8,
            .relation_id = k_relation,
            .predicate = SsiPredicate::all(),
        }
    );
    EXPECT_EQ(
        revalidate_ssi_predicate_readers(states, k_writer, *candidates),
        std::vector<base::Xid>({k_reader})
    );
}

}  // namespace
}  // namespace db::transaction::detail
