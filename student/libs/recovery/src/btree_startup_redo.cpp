#include "db/recovery/btree_startup_redo.hpp"

#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/recovery/btree_wal.hpp"
#include "db/recovery/dispatcher.hpp"

#include "btree_redo_internal.hpp"
#include "startup_port_boundary.hpp"

namespace db::recovery {
namespace {

[[maybe_unused]] auto startup_redo_error(const base::ErrorCode code, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "recovery.btree_startup_redo",
        "replay B-tree record"
    );
}

[[maybe_unused]] auto make_page_request([[maybe_unused]] const RecoveryBlockDispatch& block)
    -> StartupPageRequest {
    // LAB-S10.Q09:R01-BEGIN
    base::lab::missing_algorithm("S10.Q09:R01");
    // LAB-S10.Q09:R01-END
}

}  // namespace

[[maybe_unused]] auto replay_btree_startup_redo(
    [[maybe_unused]] const wal::DecodedWalRecord& decoded,
    [[maybe_unused]] StartupPageAccessPort& pages
) -> base::Result<BtreeStartupRedoResult> {
    // LAB-S10.Q09:R02-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R02"});
    // LAB-S10.Q09:R02-END
}

}  // namespace db::recovery
