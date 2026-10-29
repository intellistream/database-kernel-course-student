#include "db/recovery/startup_recovery.hpp"

static_assert(
    db::recovery::StartupRecoveryDisposition::kRequiresReplay
    != db::recovery::StartupRecoveryDisposition::kCleanStart
);
