#pragma once

#include <cstdint>
#include <optional>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/proc_array.hpp"
#include "db/transaction/state.hpp"

namespace db::transaction {

// Heap-independent MVCC metadata. Access maps version-header flags onto this
// value so transaction visibility does not depend on a physical tuple layout.
struct TupleVersionState final {
    base::Xid xmin{base::kInvalidXid};
    base::Xid xmax{base::kInvalidXid};
    std::uint32_t command_id{0};
    bool xmax_lock_only{false};
    bool xmax_deleted{false};

    auto operator==(const TupleVersionState&) const -> bool = default;
};

enum class TupleModificationResult : std::uint8_t {
    kOk,
    kInvisible,
    kSelfModified,
    kUpdated,
    kDeleted,
    kBeingModified,
};

struct TupleModificationDecision final {
    TupleModificationResult result{TupleModificationResult::kInvisible};
    // Set only for kBeingModified; otherwise invalid.
    base::Xid blocking_xid{base::kInvalidXid};

    auto operator==(const TupleModificationDecision&) const -> bool = default;
};

auto xid_in_mvcc_snapshot(base::Xid xid, const Snapshot& snapshot) noexcept -> bool;

auto tuple_visible_in_snapshot(
    const TupleVersionState& tuple,
    base::Xid own_xid,
    const Snapshot& snapshot,
    const CommitLog& commit_log
) noexcept -> bool;

auto tuple_satisfies_update(
    const TupleVersionState& tuple,
    base::Xid own_xid,
    const Snapshot& snapshot,
    const CommitLog& commit_log
) noexcept -> TupleModificationDecision;

// Per-session bridge from TransactionStateMachine actions to ProcArray data.
// The ProcArray and lease must outlive this non-owning coordinator. A captured
// transaction snapshot is immutable except for the command ID attached to the
// returned value on each reuse.
class SnapshotCoordinator final {
public:
    SnapshotCoordinator(ProcArray& proc_array, ProcSlotLease& lease) noexcept;
    ~SnapshotCoordinator() = default;
    SnapshotCoordinator(const SnapshotCoordinator&) = delete;
    auto operator=(const SnapshotCoordinator&) -> SnapshotCoordinator& = delete;
    SnapshotCoordinator(SnapshotCoordinator&&) = delete;
    auto operator=(SnapshotCoordinator&&) -> SnapshotCoordinator& = delete;

    auto acquire(const TransactionStart& start) -> base::Result<Snapshot>;
    auto acquire(const CommandStart& start) -> base::Result<Snapshot>;

    // Call before durable completion when a session wants to stop advertising
    // its snapshot early. TransactionManager completion also clears the slot;
    // a later XID is detected and resets local cached state automatically.
    auto release(base::Xid expected_xid) -> base::Status;

    auto has_transaction_snapshot() const noexcept -> bool;
    auto transaction_epoch() const noexcept -> std::uint64_t;

private:
    auto acquire(SnapshotAction action, std::uint64_t epoch, std::uint32_t command_id)
        -> base::Result<Snapshot>;
    void reset_local_state() noexcept;

    ProcArray* proc_array_;
    ProcSlotLease* lease_;
    base::Xid owner_xid_{base::kInvalidXid};
    std::uint64_t transaction_epoch_{0};
    std::optional<RegisteredSnapshot> command_snapshot_cache_;
    std::optional<Snapshot> transaction_snapshot_;
};

}  // namespace db::transaction
