#include "db/recovery/startup_recovery.hpp"

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::recovery {

[[maybe_unused]] auto
startup_recovery_plan([[maybe_unused]] const wal::WalControlImage& control_image)
    -> base::Result<StartupRecoveryPlan> {
    // LAB-S10.Q11:R04-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q11:R04"});
    // LAB-S10.Q11:R04-END
}

}  // namespace db::recovery
