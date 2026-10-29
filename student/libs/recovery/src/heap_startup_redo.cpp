#include "db/recovery/heap_startup_redo.hpp"

#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/recovery/dispatcher.hpp"

#include "heap_redo_internal.hpp"
#include "startup_port_boundary.hpp"

namespace db::recovery {
namespace {

[[maybe_unused]] auto startup_redo_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "recovery.heap_startup_redo",
        "replay heap record"
    );
}

[[maybe_unused]] auto make_page_request([[maybe_unused]] const RecoveryRecordDispatch& dispatch)
    -> base::Result<StartupPageRequest> {
    // LAB-S10.Q09:R10-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R10"});
    // LAB-S10.Q09:R10-END
}

}  // namespace

[[maybe_unused]] auto replay_heap_startup_redo(
    [[maybe_unused]] const wal::DecodedWalRecord& decoded,
    [[maybe_unused]] StartupPageAccessPort& pages
) -> base::Result<HeapRedoDisposition> {
    // LAB-S10.Q09:R11-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R11"});
    // LAB-S10.Q09:R11-END
}

}  // namespace db::recovery
