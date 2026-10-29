#pragma once

#include <cstdint>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/backend.hpp"
#include "db/transaction/manager.hpp"
#include "db/transaction/visibility.hpp"

namespace db::transaction::detail {

// Owns one backend's stable ProcArray lease and the shared-service state tied
// to its current XID. Frontend block transitions remain in TransactionBackend.
class BackendTransactionServices final {
public:
    BackendTransactionServices(
        TransactionManager& manager,
        ProcSlotLease lease,
        TransactionBackendOptions options
    ) noexcept;
    ~BackendTransactionServices() = default;
    BackendTransactionServices(const BackendTransactionServices&) = delete;
    auto operator=(const BackendTransactionServices&) -> BackendTransactionServices& = delete;
    BackendTransactionServices(BackendTransactionServices&&) = delete;
    auto operator=(BackendTransactionServices&&) -> BackendTransactionServices& = delete;

    auto xid() const noexcept -> base::Xid;
    auto has_persistent_writes() const noexcept -> bool;
    auto proc_slot() const noexcept -> const ProcSlot*;

    auto start_transaction(IsolationLevel isolation) -> base::Status;
    auto capture_snapshot(const TransactionStart& start) -> base::Result<Snapshot>;
    auto capture_snapshot(const CommandStart& start) -> base::Result<Snapshot>;
    auto commit_transaction() -> base::Status;
    auto abort_transaction() -> base::Status;
    auto abort_after_error(base::Error primary_error) -> base::Error;
    auto mark_persistent_write() -> base::Status;
    auto mark_unlogged_write() -> base::Status;
    auto close() -> base::Status;

private:
    enum class PersistentWriteMode : std::uint8_t {
        kNone,
        kWal,
        kUnlogged,
    };

    auto ensure_serializable_registration() -> base::Status;
    auto attach_serializable_snapshot(const Snapshot& snapshot) -> base::Status;
    void reset_transaction() noexcept;

    TransactionManager* manager_;
    ProcSlotLease lease_;
    SnapshotCoordinator snapshots_;
    TransactionBackendOptions options_;
    base::Xid xid_{base::kInvalidXid};
    bool has_persistent_writes_{false};
    PersistentWriteMode persistent_write_mode_{PersistentWriteMode::kNone};
    bool ssi_registered_{false};
    bool ssi_snapshot_set_{false};
};

}  // namespace db::transaction::detail
