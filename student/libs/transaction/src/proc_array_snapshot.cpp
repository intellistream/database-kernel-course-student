#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/proc_array.hpp"

#include "proc_array_state.hpp"

namespace db::transaction {
namespace {

using detail::proc_array_error;

[[maybe_unused]] auto xid_min(base::Xid left, base::Xid right) noexcept -> base::Xid {
    return left.value() < right.value() ? left : right;
}

}  // namespace

[[maybe_unused]] auto Snapshot::contains([[maybe_unused]] base::Xid xid) const noexcept -> bool {
    // LAB-S09.Q01:R01-BEGIN
    base::lab::missing_algorithm("S09.Q01:R01");
    // LAB-S09.Q01:R01-END
}

[[maybe_unused]] auto ProcArray::snapshot([[maybe_unused]] base::Xid owner_xid) const
    -> base::Result<Snapshot> {
    // LAB-S09.Q01:R03-BEGIN
    base::lab::missing_algorithm("S09.Q01:R03");
    // LAB-S09.Q01:R03-END
}

[[maybe_unused]] auto ProcArray::snapshot_and_register(
    [[maybe_unused]] ProcSlotLease& lease,
    [[maybe_unused]] const RegisteredSnapshot* previous
) -> base::Result<SnapshotCapture> {
    // LAB-S09.Q01:R07-BEGIN
    base::lab::missing_algorithm("S09.Q01:R07");
    // LAB-S09.Q01:R07-END
}

[[maybe_unused]] auto ProcArray::clear_registered_snapshot(
    [[maybe_unused]] ProcSlotLease& lease,
    [[maybe_unused]] base::Xid expected_xid
) -> base::Status {
    // LAB-S09.Q01:R08-BEGIN
    base::lab::missing_algorithm("S09.Q01:R08");
    // LAB-S09.Q01:R08-END
}

[[maybe_unused]] auto ProcArray::oldest_active_xmin() const -> base::Xid {
    // LAB-S09.Q01:R02-BEGIN
    base::lab::missing_algorithm("S09.Q01:R02");
    // LAB-S09.Q01:R02-END
}

[[maybe_unused]] auto ProcArray::build_snapshot_locked([[maybe_unused]] base::Xid owner_xid) const
    -> std::pair<Snapshot, base::Xid> {
    // LAB-S09.Q01:R06-BEGIN
    base::lab::missing_algorithm("S09.Q01:R06");
    // LAB-S09.Q01:R06-END
}

}  // namespace db::transaction
