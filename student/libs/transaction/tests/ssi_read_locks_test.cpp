#include <cstdint>

#include <gtest/gtest.h>

#include "db/base/identifier.hpp"
#include "db/transaction/ssi.hpp"

#include "ssi_read_locks.hpp"
#include "ssi_state.hpp"

namespace db::transaction::detail {
namespace {

TEST(SsiReadLocksTest, PagePromotionReplacesTupleTargetsInTheOwnerAndIndex) {
    constexpr auto k_reader = base::Xid{100};
    constexpr auto k_relation = base::RelationId{1};
    constexpr auto k_page = base::PageId{4};
    SsiStateMap states;
    auto [state, inserted] = states.emplace(k_reader, SsiTransactionState{1});
    ASSERT_TRUE(inserted);
    SsiTargetIndex index;

    for (std::uint16_t slot = 0; slot < 3U; ++slot) {
        const auto target =
            SsiReadTarget::for_tuple(k_relation, base::Rid{k_page, base::SlotId{slot}});
        state->second.read_locks.push_back(SsiReadRecord{.target = target});
        add_read_lock_holder(index, target, k_reader);
    }

    promote_read_locks_to_page(states, index, k_reader, k_relation, k_page);

    ASSERT_EQ(state->second.read_locks.size(), 1U);
    EXPECT_EQ(state->second.read_locks.front().target, SsiReadTarget::for_page(k_relation, k_page));
    ASSERT_EQ(index.size(), 1U);
    EXPECT_EQ(
        index.at(SsiReadTarget::for_page(k_relation, k_page)),
        std::vector<base::Xid>({k_reader})
    );
}

TEST(SsiReadLocksTest, RelationPromotionRebuildsOnlyThePromotedOwnersTargets) {
    constexpr auto k_reader = base::Xid{100};
    constexpr auto k_other = base::Xid{200};
    constexpr auto k_relation = base::RelationId{1};
    const auto reader_page = SsiReadTarget::for_page(k_relation, base::PageId{4});
    const auto other_page = SsiReadTarget::for_page(base::RelationId{2}, base::PageId{5});
    SsiStateMap states;
    states.emplace(k_reader, SsiTransactionState{1})
        .first->second.read_locks.push_back(SsiReadRecord{.target = reader_page});
    states.emplace(k_other, SsiTransactionState{1})
        .first->second.read_locks.push_back(SsiReadRecord{.target = other_page});
    SsiTargetIndex index;
    rebuild_read_lock_index(states, index);

    promote_read_locks_to_relation(states, index, k_reader, k_relation);

    EXPECT_TRUE(index.contains(SsiReadTarget::for_relation(k_relation)));
    EXPECT_FALSE(index.contains(reader_page));
    EXPECT_EQ(index.at(other_page), std::vector<base::Xid>({k_other}));
}

}  // namespace
}  // namespace db::transaction::detail
