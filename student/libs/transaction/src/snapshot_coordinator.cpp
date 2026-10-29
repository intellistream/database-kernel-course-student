#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/visibility.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction {
namespace {

[[maybe_unused]] auto
visibility_error(base::ErrorCode code, std::string message, std::string operation) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "transaction/visibility",
        std::move(operation)
    );
}

}  // namespace

SnapshotCoordinator::SnapshotCoordinator(ProcArray& proc_array, ProcSlotLease& lease) noexcept
    : proc_array_{&proc_array},
      lease_{&lease} {}

[[maybe_unused]] auto SnapshotCoordinator::acquire([[maybe_unused]] const TransactionStart& start)
    -> base::Result<Snapshot> {
    // LAB-S09.Q02:R01-BEGIN
    base::lab::missing_algorithm("S09.Q02:R01");
    // LAB-S09.Q02:R01-END
}

[[maybe_unused]] auto SnapshotCoordinator::acquire([[maybe_unused]] const CommandStart& start)
    -> base::Result<Snapshot> {
    // LAB-S09.Q02:R2001-BEGIN
    base::lab::missing_algorithm("S09.Q02:R2001");
    // LAB-S09.Q02:R2001-END
}

[[maybe_unused]] auto SnapshotCoordinator::acquire(
    [[maybe_unused]] SnapshotAction action,
    [[maybe_unused]] std::uint64_t epoch,
    [[maybe_unused]] std::uint32_t command_id
) -> base::Result<Snapshot> {
    // LAB-S09.Q02:R2002-BEGIN
    base::lab::missing_algorithm("S09.Q02:R2002");
    // LAB-S09.Q02:R2002-END
}

[[maybe_unused]] auto SnapshotCoordinator::release([[maybe_unused]] base::Xid expected_xid)
    -> base::Status {
    // LAB-S09.Q02:R02-BEGIN
    base::lab::missing_algorithm("S09.Q02:R02");
    // LAB-S09.Q02:R02-END
}

auto SnapshotCoordinator::has_transaction_snapshot() const noexcept -> bool {
    return transaction_snapshot_.has_value();
}

auto SnapshotCoordinator::transaction_epoch() const noexcept -> std::uint64_t {
    return transaction_epoch_;
}

[[maybe_unused]] void SnapshotCoordinator::reset_local_state() noexcept {
    // LAB-S09.Q02:R03-BEGIN
    base::lab::missing_algorithm("S09.Q02:R03");
    // LAB-S09.Q02:R03-END
}

}  // namespace db::transaction
