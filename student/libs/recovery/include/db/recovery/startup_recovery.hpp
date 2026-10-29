#pragma once

#include <cstdint>

#include "db/base/error.hpp"
#include "db/base/result.hpp"
#include "db/wal/control.hpp"

namespace db::recovery {

enum class StartupRecoveryDisposition : std::uint8_t {
    kCleanStart,
    kRequiresReplay,
};

struct StartupRecoveryPlan final {
    wal::DatabaseState control_state{wal::DatabaseState::kShutdown};
    StartupRecoveryDisposition disposition{StartupRecoveryDisposition::kCleanStart};

    auto requires_replay() const noexcept -> bool {
        return disposition == StartupRecoveryDisposition::kRequiresReplay;
    }
};

// Classify the durable control state at process startup.
// - kShutdown -> clean startup, no recovery dispatch.
// - kStartingUp / kInCrashRecovery / kShuttingDown / kInProduction -> an
//   unclean boundary that must replay before sessions become available.
// A process can die while production or shutdown work is in progress, so
// neither state is proof of a clean durable boundary.
auto startup_recovery_plan(const wal::WalControlImage& control_image)
    -> base::Result<StartupRecoveryPlan>;

}  // namespace db::recovery
