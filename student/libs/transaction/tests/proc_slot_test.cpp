#include <cstdint>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/transaction/proc_slot.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction {
namespace {

TEST(ProcSlotTest, RejectsInvalidIdentityAndTransactionIds) {
    auto invalid_slot = ProcSlot::create(base::kInvalidSlotId);
    ASSERT_FALSE(invalid_slot);
    EXPECT_EQ(invalid_slot.error().code(), base::ErrorCode::kInvalidArgument);

    auto slot = ProcSlot::create(base::SlotId{7});
    ASSERT_TRUE(slot);
    auto invalid_xid = (*slot)->assign_xid(base::kInvalidXid);
    ASSERT_FALSE(invalid_xid);
    EXPECT_EQ(invalid_xid.error().code(), base::ErrorCode::kInvalidArgument);
    auto boundary_xid = (*slot)->assign_xid(kXidWrapBoundary);
    ASSERT_FALSE(boundary_xid);
    EXPECT_EQ(boundary_xid.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(ProcSlotTest, StableSlotPublishesClearsAndReusesAcrossTransactions) {
    auto created = ProcSlot::create(base::SlotId{11});
    ASSERT_TRUE(created);
    auto slot = std::move(*created);
    const auto* const stable_address = slot.get();
    EXPECT_EQ(slot->slot_id(), base::SlotId{11});
    EXPECT_FALSE(slot->has_active_transaction());

    ASSERT_TRUE(slot->assign_xid(base::Xid{41}));
    EXPECT_TRUE(slot->has_active_transaction());
    EXPECT_EQ(slot->xid(), base::Xid{41});
    auto duplicate = slot->assign_xid(base::Xid{42});
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code(), base::ErrorCode::kTransactionState);
    EXPECT_EQ(slot->xid(), base::Xid{41});

    auto wrong_clear = slot->clear_xid(base::Xid{42});
    ASSERT_FALSE(wrong_clear);
    EXPECT_EQ(wrong_clear.error().code(), base::ErrorCode::kInvariantViolation);
    EXPECT_EQ(slot->xid(), base::Xid{41});
    ASSERT_TRUE(slot->clear_xid(base::Xid{41}));
    EXPECT_FALSE(slot->has_active_transaction());

    auto duplicate_clear = slot->clear_xid(base::Xid{41});
    ASSERT_FALSE(duplicate_clear);
    EXPECT_EQ(duplicate_clear.error().code(), base::ErrorCode::kTransactionState);
    ASSERT_TRUE(slot->assign_xid(base::Xid{43}));
    EXPECT_EQ(slot.get(), stable_address);
    EXPECT_EQ(slot->slot_id(), base::SlotId{11});
    EXPECT_EQ(slot->xid(), base::Xid{43});
}

}  // namespace
}  // namespace db::transaction
