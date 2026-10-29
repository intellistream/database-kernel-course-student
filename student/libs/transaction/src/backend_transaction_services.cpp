#include "backend_transaction_services.hpp"

#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction::detail {
namespace {

[[maybe_unused]] auto backend_error(std::string message, std::string operation) -> base::Error {
    return base::Error{base::ErrorCode::kTransactionState, std::move(message)}.with_context(
        "transaction/backend",
        std::move(operation)
    );
}

}  // namespace

[[maybe_unused]] BackendTransactionServices::BackendTransactionServices(
    [[maybe_unused]] TransactionManager& manager,
    [[maybe_unused]] ProcSlotLease lease,
    [[maybe_unused]] const TransactionBackendOptions options
) noexcept
    : manager_{&manager},
      lease_{std::move(lease)},
      snapshots_{manager.snapshot_coordinator(lease_)},
      options_{options} {
    // LAB-S08.Q07:R31-BEGIN
    base::lab::missing_algorithm("S08.Q07:R31");
    // LAB-S08.Q07:R31-END
}

auto BackendTransactionServices::xid() const noexcept -> base::Xid {
    return xid_;
}

auto BackendTransactionServices::has_persistent_writes() const noexcept -> bool {
    return has_persistent_writes_;
}

auto BackendTransactionServices::proc_slot() const noexcept -> const ProcSlot* {
    return lease_.slot();
}

[[maybe_unused]] auto
BackendTransactionServices::start_transaction([[maybe_unused]] const IsolationLevel isolation)
    -> base::Status {
    // LAB-S08.Q06:R24-BEGIN
    base::lab::missing_algorithm("S08.Q06:R24");
    // LAB-S08.Q06:R24-END
}

[[maybe_unused]] auto
BackendTransactionServices::capture_snapshot([[maybe_unused]] const TransactionStart& start)
    -> base::Result<Snapshot> {
    // LAB-S08.Q06:R25-BEGIN
    base::lab::missing_algorithm("S08.Q06:R25");
    // LAB-S08.Q06:R25-END
}

[[maybe_unused]] auto
BackendTransactionServices::capture_snapshot([[maybe_unused]] const CommandStart& start)
    -> base::Result<Snapshot> {
    // LAB-S08.Q06:R26-BEGIN
    base::lab::missing_algorithm("S08.Q06:R26");
    // LAB-S08.Q06:R26-END
}

[[maybe_unused]] auto BackendTransactionServices::commit_transaction() -> base::Status {
    // LAB-S08.Q06:R21-BEGIN
    base::lab::missing_algorithm("S08.Q06:R21");
    // LAB-S08.Q06:R21-END
}

[[maybe_unused]] auto BackendTransactionServices::abort_transaction() -> base::Status {
    // LAB-S08.Q06:R22-BEGIN
    base::lab::missing_algorithm("S08.Q06:R22");
    // LAB-S08.Q06:R22-END
}

[[maybe_unused]] auto
BackendTransactionServices::abort_after_error([[maybe_unused]] base::Error primary_error)
    -> base::Error {
    // LAB-S08.Q06:R23-BEGIN
    base::lab::missing_algorithm("S08.Q06:R23");
    // LAB-S08.Q06:R23-END
}

[[maybe_unused]] auto BackendTransactionServices::mark_persistent_write() -> base::Status {
    // LAB-S08.Q06:R27-BEGIN
    base::lab::missing_algorithm("S08.Q06:R27");
    // LAB-S08.Q06:R27-END
}

[[maybe_unused]] auto BackendTransactionServices::mark_unlogged_write() -> base::Status {
    // LAB-S08.Q06:R28-BEGIN
    base::lab::missing_algorithm("S08.Q06:R28");
    // LAB-S08.Q06:R28-END
}

auto BackendTransactionServices::close() -> base::Status {
    if (is_normal_xid(xid_)) {
        return base::fail(
            backend_error("cannot close a backend with an active transaction", "close")
        );
    }
    auto status = lease_.close();
    if (!status) {
        return base::fail(std::move(status).error().with_context("transaction/backend", "close"));
    }
    return {};
}

[[maybe_unused]] auto BackendTransactionServices::ensure_serializable_registration()
    -> base::Status {
    // LAB-S08.Q07:R32-BEGIN
    base::lab::missing_algorithm("S08.Q07:R32");
    // LAB-S08.Q07:R32-END
}

[[maybe_unused]] auto
BackendTransactionServices::attach_serializable_snapshot([[maybe_unused]] const Snapshot& snapshot)
    -> base::Status {
    // LAB-S08.Q07:R33-BEGIN
    base::lab::missing_algorithm("S08.Q07:R33");
    // LAB-S08.Q07:R33-END
}

[[maybe_unused]] void BackendTransactionServices::reset_transaction() noexcept {
    // LAB-S08.Q06:R29-BEGIN
    base::lab::missing_algorithm("S08.Q06:R29");
    // LAB-S08.Q06:R29-END
}
}  // namespace db::transaction::detail
