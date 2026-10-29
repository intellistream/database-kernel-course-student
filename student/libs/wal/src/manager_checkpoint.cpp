#include "manager_checkpoint.hpp"

#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/wal/control_store.hpp"
#include "db/wal/writer.hpp"

namespace db::wal::manager_detail {
namespace {

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.manager",
        "validate"
    );
}

}  // namespace

[[maybe_unused]] auto complete_checkpoint_publication(
    [[maybe_unused]] WalWriter& writer,
    [[maybe_unused]] WalControlStore& control_store,
    [[maybe_unused]] const base::Lsn current_redo_lsn,
    [[maybe_unused]] const base::Lsn current_end_lsn,
    [[maybe_unused]] WalControlImage next_control,
    [[maybe_unused]] const base::Lsn checkpoint_end_lsn
) -> base::Status {
    // LAB-S10.Q07:R1018-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q07:R1018"});
    // LAB-S10.Q07:R1018-END
}

}  // namespace db::wal::manager_detail
