#pragma once

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/wal/control.hpp"

namespace db::wal {

class WalControlStore;
class WalWriter;

namespace manager_detail {

auto complete_checkpoint_publication(
    WalWriter& writer,
    WalControlStore& control_store,
    base::Lsn current_redo_lsn,
    base::Lsn current_end_lsn,
    WalControlImage next_control,
    base::Lsn checkpoint_end_lsn
) -> base::Status;

}  // namespace manager_detail
}  // namespace db::wal
