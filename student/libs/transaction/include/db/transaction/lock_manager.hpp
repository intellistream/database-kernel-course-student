#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::transaction {

class XidWaiter;

namespace detail {
class TransactionCompletionAccess;
}  // namespace detail

enum class TableLockMode : std::uint8_t {
    kAccessShare = 0,
    kRowShare = 1,
    kRowExclusive = 2,
    kShareUpdateExclusive = 3,
    kShare = 4,
    kShareRowExclusive = 5,
    kExclusive = 6,
    kAccessExclusive = 7,
};

enum class RowLockMode : std::uint8_t {
    kKeyShare = 0,
    kShare = 1,
    kNoKeyExclusive = 2,
    kExclusive = 3,
};

auto table_lock_mode_name(TableLockMode mode) noexcept -> std::string_view;
auto row_lock_mode_name(RowLockMode mode) noexcept -> std::string_view;
auto table_lock_modes_compatible(TableLockMode left, TableLockMode right) noexcept -> bool;
auto row_lock_modes_compatible(RowLockMode left, RowLockMode right) noexcept -> bool;
auto table_lock_mode_subsumes(TableLockMode stronger, TableLockMode weaker) noexcept -> bool;
auto row_lock_mode_subsumes(RowLockMode stronger, RowLockMode weaker) noexcept -> bool;

struct LockManagerOptions final {
    bool detect_deadlocks{true};
    std::chrono::milliseconds deadlock_timeout{std::chrono::seconds{1}};
};

struct LockManagerStats final {
    std::size_t table_targets{0};
    std::size_t row_targets{0};
    std::size_t transaction_targets{0};
    std::size_t granted_locks{0};
    std::size_t waiting_requests{0};
    std::size_t transaction_wait_targets{0};
    std::size_t transaction_waiting_requests{0};

    auto operator==(const LockManagerStats&) const -> bool = default;
};

// Transaction-scoped row/table lock service. Acquisitions block in FIFO order;
// release_all() is the normal two-phase-locking release boundary for explicit
// table and row locks. TransactionManager privately owns XID-tag publication.
class LockManager final {
public:
    LockManager();
    explicit LockManager(const LockManagerOptions& options);
    ~LockManager();
    LockManager(const LockManager&) = delete;
    auto operator=(const LockManager&) -> LockManager& = delete;
    LockManager(LockManager&&) = delete;
    auto operator=(LockManager&&) -> LockManager& = delete;

    auto acquire_table(base::Xid xid, base::RelationId relation_id, TableLockMode mode)
        -> base::Status;
    auto upgrade_table(base::Xid xid, base::RelationId relation_id, TableLockMode mode)
        -> base::Status;

    // The caller acquires the relation intent lock before entering this
    // tuple-lock protocol. Like PostgreSQL's LockTuple(), this hot path trusts
    // that internal ordering contract instead of looking it up again.
    auto acquire_row(base::Xid xid, base::RelationId relation_id, base::Rid rid, RowLockMode mode)
        -> base::Status;
    auto upgrade_row(base::Xid xid, base::RelationId relation_id, base::Rid rid, RowLockMode mode)
        -> base::Status;

    // Cancels a pending request and releases every explicit table/row lock for
    // xid. A TransactionManager-owned XID tag is deliberately not releasable
    // through this public operation.
    auto release_all(base::Xid xid) -> base::Status;
    auto cancel_wait(base::Xid xid) -> base::Status;

    auto held_table_mode(base::Xid xid, base::RelationId relation_id) const
        -> std::optional<TableLockMode>;
    auto held_row_mode(base::Xid xid, base::RelationId relation_id, base::Rid rid) const
        -> std::optional<RowLockMode>;
    auto stats() const -> LockManagerStats;

private:
    friend class TransactionManager;
    friend class XidWaiter;
    friend class detail::TransactionCompletionAccess;

    struct Inner;

    auto register_transaction(base::Xid xid) -> base::Status;
    auto wait_for_transaction(base::Xid waiter_xid, base::Xid blocking_xid) -> base::Status;
    auto finish_transaction(base::Xid xid) -> base::Status;

    std::unique_ptr<Inner> inner_;
};

}  // namespace db::transaction
