#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/recovery/startup_recovery.hpp"
#include "db/transaction/xid.hpp"
#include "db/wal/format.hpp"

namespace db::recovery {
namespace {

auto control_with_state(const wal::DatabaseState state) -> wal::WalControlImage {
    return wal::WalControlImage{
        .system_identifier = 0xA1B2'0C3D'4E5F'6A7BULL,
        .generation = 7U,
        .checkpoint_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .next_xid = transaction::kFirstNormalXid,
        .latest_completed_xid = base::kInvalidXid,
        .next_object_id = 1U,
        .state = state,
        .checksum = 0U,
    };
}

TEST(StartupRecoveryTest, ClassifiesCleanShutdown) {
    auto plan = startup_recovery_plan(control_with_state(wal::DatabaseState::kShutdown));
    ASSERT_TRUE(plan);
    EXPECT_FALSE(plan->requires_replay());
    EXPECT_EQ(plan->control_state, wal::DatabaseState::kShutdown);
}

TEST(StartupRecoveryTest, ClassifiesEveryUncleanStartupStateAsRecoverable) {
    auto starting = startup_recovery_plan(control_with_state(wal::DatabaseState::kStartingUp));
    ASSERT_TRUE(starting);
    EXPECT_TRUE(starting->requires_replay());
    EXPECT_EQ(starting->control_state, wal::DatabaseState::kStartingUp);

    auto recovering =
        startup_recovery_plan(control_with_state(wal::DatabaseState::kInCrashRecovery));
    ASSERT_TRUE(recovering);
    EXPECT_TRUE(recovering->requires_replay());
    EXPECT_EQ(recovering->control_state, wal::DatabaseState::kInCrashRecovery);

    auto shutting_down =
        startup_recovery_plan(control_with_state(wal::DatabaseState::kShuttingDown));
    ASSERT_TRUE(shutting_down);
    EXPECT_TRUE(shutting_down->requires_replay());
    EXPECT_EQ(shutting_down->control_state, wal::DatabaseState::kShuttingDown);

    auto in_production =
        startup_recovery_plan(control_with_state(wal::DatabaseState::kInProduction));
    ASSERT_TRUE(in_production);
    EXPECT_TRUE(in_production->requires_replay());
    EXPECT_EQ(in_production->control_state, wal::DatabaseState::kInProduction);
}

}  // namespace
}  // namespace db::recovery
