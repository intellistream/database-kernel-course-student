#include "db/recovery/startup_runner.hpp"

#include <exception>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/recovery/btree_startup_redo.hpp"
#include "db/recovery/heap_startup_redo.hpp"
#include "db/recovery/wal_stream.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

[[maybe_unused]] auto startup_runner_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "recovery.startup_runner",
        "replay startup WAL"
    );
}

// A failed pass must not let RecoveryPageCache's deliberate destructor
// safeguard turn a structured startup failure into silent data loss. WAL and
// the non-production control state remain authoritative, so the cache may be
// explicitly discarded for a fresh idempotent replay attempt.
class CacheRetryGuard final {
public:
    explicit CacheRetryGuard(RecoveryPageCache& page_cache) noexcept
        : page_cache_{page_cache} {}

    CacheRetryGuard(const CacheRetryGuard&) = delete;
    auto operator=(const CacheRetryGuard&) -> CacheRetryGuard& = delete;
    CacheRetryGuard(CacheRetryGuard&&) = delete;
    auto operator=(CacheRetryGuard&&) -> CacheRetryGuard& = delete;

    ~CacheRetryGuard() {
        if (completed_) {
            return;
        }
        if (auto status = page_cache_.abandon_for_retry(); !status) {
            std::terminate();
        }
    }

    [[maybe_unused]] void complete() noexcept {
        completed_ = true;
    }

private:
    RecoveryPageCache& page_cache_;
    bool completed_{false};
};

[[maybe_unused]] auto unsupported_catalog_record() -> base::Error {
    // LAB-S10.Q11:R05-BEGIN
    base::lab::missing_algorithm("S10.Q11:R05");
    // LAB-S10.Q11:R05-END
}

[[maybe_unused]] auto replay_system_record(
    [[maybe_unused]] const wal::DecodedWalRecord& record,
    [[maybe_unused]] StartupRecoveryReport& report
) -> base::Status {
    // LAB-S10.Q11:R06-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q11:R06"});
    // LAB-S10.Q11:R06-END
}

[[maybe_unused]] auto replay_heap_record(
    [[maybe_unused]] const wal::DecodedWalRecord& record,
    [[maybe_unused]] RecoveryPageCache& page_cache,
    [[maybe_unused]] StartupRecoveryReport& report
) -> base::Status {
    // LAB-S10.Q11:R07-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q11:R07"});
    // LAB-S10.Q11:R07-END
}

[[maybe_unused]] auto replay_btree_record(
    [[maybe_unused]] const wal::DecodedWalRecord& record,
    [[maybe_unused]] RecoveryPageCache& page_cache,
    [[maybe_unused]] StartupRecoveryReport& report
) -> base::Status {
    // LAB-S10.Q11:R08-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q11:R08"});
    // LAB-S10.Q11:R08-END
}

[[maybe_unused]] auto route_physical_record(
    [[maybe_unused]] const wal::DecodedWalRecord& record,
    [[maybe_unused]] const StartupRecoveryRunContext& context,
    [[maybe_unused]] StartupRecoveryReport& report
) -> base::Status {
    // LAB-S10.Q11:R09-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q11:R09"});
    // LAB-S10.Q11:R09-END
}

}  // namespace

[[maybe_unused]] auto run_startup_recovery([[maybe_unused]] const StartupRecoveryRunContext context)
    -> base::Result<StartupRecoveryReport> {
    // LAB-S10.Q11:R10-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q11:R10"});
    // LAB-S10.Q11:R10-END
}

}  // namespace db::recovery
