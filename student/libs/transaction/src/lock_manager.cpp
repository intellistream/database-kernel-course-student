#include "db/transaction/lock_manager.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/base/lab_missing.hpp"
#include "db/transaction/xid.hpp"

#include "lock_manager_state.hpp"
#include "lock_mode_policy.hpp"
#include "lock_queue.hpp"
#include "lock_wait_graph.hpp"

namespace db::transaction {
namespace {

using detail::kTransactionRunningMode;
using detail::kTransactionWaitMode;
using detail::lock_mode_subsumes;
using detail::lock_request_compatible_with_holders;
using detail::LockRequest;
using detail::LockState;
using detail::LockTarget;
using detail::LockTargetKind;
using detail::valid_row_mode;
using detail::valid_table_mode;

[[maybe_unused]] auto lock_error(base::ErrorCode code, std::string message, std::string operation)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "transaction/lock-manager",
        std::move(operation)
    );
}

[[maybe_unused]] auto table_target(base::RelationId relation_id) noexcept -> LockTarget {
    return LockTarget{.kind = LockTargetKind::kTable, .relation_id = relation_id};
}

[[maybe_unused]] auto row_target(base::RelationId relation_id, base::Rid rid) noexcept
    -> LockTarget {
    return LockTarget{.kind = LockTargetKind::kRow, .relation_id = relation_id, .rid = rid};
}

[[maybe_unused]] auto transaction_target(base::Xid xid) noexcept -> LockTarget {
    return LockTarget{.kind = LockTargetKind::kTransaction, .transaction_xid = xid};
}

[[maybe_unused]] auto valid_target(const LockTarget& target) noexcept -> bool {
    switch (target.kind) {
        case LockTargetKind::kTable:
            return target.relation_id != base::kInvalidRelationId;
        case LockTargetKind::kRow:
            return target.relation_id != base::kInvalidRelationId
                   && target.rid.page_id() != base::kInvalidPageId
                   && target.rid.slot_id() != base::kInvalidSlotId;
        case LockTargetKind::kTransaction:
            return is_normal_xid(target.transaction_xid);
    }
    std::unreachable();
}

inline constexpr std::size_t kLockCacheLineSize = 64U;
inline constexpr std::size_t kLockStateCacheCapacity = 8U;
inline constexpr std::size_t kLockOwnerCacheCapacity = 8U;

struct alignas(kLockCacheLineSize) LockTargetShard final {
    mutable std::mutex mutex;
    detail::LockTable locks;
    std::array<detail::LockTable::node_type, kLockStateCacheCapacity> cached_lock_states;
    std::size_t cached_lock_state_count{0U};
};

struct LockOwnerState final {
    std::vector<LockTarget> granted;
    std::shared_ptr<LockRequest> waiting;
};

using LockOwnerTable = std::unordered_map<base::Xid, LockOwnerState>;

struct LockOwnerRelease final {
    std::vector<LockTarget> granted;
    std::shared_ptr<LockRequest> waiting;
    LockOwnerTable::node_type cached_owner;
};

struct alignas(kLockCacheLineSize) LockOwnerShard final {
    mutable std::mutex mutex;
    LockOwnerTable owners;
    std::array<LockOwnerTable::node_type, kLockOwnerCacheCapacity> cached_owners;
    std::size_t cached_owner_count{0U};
};

}  // namespace

struct LockManager::Inner final {
    explicit Inner(const LockManagerOptions& stored_options)
        : options{stored_options} {}

    auto acquire(
        const LockTarget& target,
        base::Xid xid,
        std::uint8_t mode,
        bool upgrade,
        bool transient = false
    ) -> base::Status;
    auto acquire_new_target(
        LockTargetShard& shard,
        const LockTarget& target,
        base::Xid xid,
        std::uint8_t mode,
        bool upgrade,
        bool transient
    ) -> base::Status;
    auto try_immediate_grant(
        LockState& state,
        const LockTarget& target,
        base::Xid xid,
        std::uint8_t mode,
        bool upgrade,
        bool transient
    ) -> base::Result<bool>;
    auto enqueue_and_wait(
        std::unique_lock<std::mutex>& lock,
        const LockTarget& target,
        LockState& state,
        base::Xid xid,
        std::uint8_t mode,
        bool upgrade,
        bool transient
    ) -> base::Status;
    auto check_deadlock(const std::shared_ptr<LockRequest>& request) -> base::Status;
    auto
    has_newer_cycle_waiter(const detail::LockWaitGraph& graph, const LockRequest& request) const
        -> bool;
    auto release_all(base::Xid xid) -> base::Status;
    auto finish_transaction(base::Xid xid) -> base::Status;
    auto cancel_wait(base::Xid xid) -> base::Status;
    void grant_waiters(LockState& state);
    auto wait_graph() const -> detail::LockWaitGraph;
    void remove_request(LockState& state, const std::shared_ptr<LockRequest>& request);
    auto record_immediate_grant(base::Xid xid, const LockTarget& target, bool upgrade)
        -> base::Status;
    void recycle_lock_state(
        LockTargetShard& shard,
        detail::LockTable::iterator state,
        detail::LockHolders::node_type holder
    );
    void record_grant(const LockRequest& request);
    void clear_waiting(const LockRequest& request);
    auto waiting_request(base::Xid xid) const -> std::shared_ptr<LockRequest>;
    auto cancel_request(const std::shared_ptr<LockRequest>& request) -> base::Status;
    auto begin_owner_release(base::Xid xid) -> LockOwnerRelease;
    auto find_or_create_owner(LockOwnerShard& shard, base::Xid xid)
        -> std::pair<LockOwnerTable::iterator, bool>;
    void recycle_owner_state(base::Xid xid, LockOwnerRelease& owner);
    auto lock_all_targets() const -> std::vector<std::unique_lock<std::mutex>>;
    void shutdown();

    auto target_shard(const LockTarget& target) noexcept -> LockTargetShard& {
        return target_shards.at(detail::lock_target_shard_index(target));
    }

    auto target_shard(const LockTarget& target) const noexcept -> const LockTargetShard& {
        return target_shards.at(detail::lock_target_shard_index(target));
    }

    auto owner_shard(base::Xid xid) noexcept -> LockOwnerShard& {
        return owner_shards.at(xid.value() % owner_shards.size());
    }

    auto owner_shard(base::Xid xid) const noexcept -> const LockOwnerShard& {
        return owner_shards.at(xid.value() % owner_shards.size());
    }

    LockManagerOptions options;
    std::array<LockTargetShard, detail::kLockTargetShardCount> target_shards;
    std::array<LockOwnerShard, 32U> owner_shards;
    std::atomic<std::uint64_t> next_wait_sequence{1U};
    std::atomic<bool> shutting_down{false};
};

namespace {

[[maybe_unused]] auto validate_request(
    [[maybe_unused]] const LockTarget& target,
    [[maybe_unused]] base::Xid xid,
    [[maybe_unused]] std::uint8_t mode
) -> base::Status {
    // LAB-S08.Q04:R18-BEGIN
    base::lab::missing_algorithm("S08.Q04:R18");
    // LAB-S08.Q04:R18-END
}

inline constexpr base::ExceptionBoundaryPolicy kDeadlockDetectionExceptionPolicy{
    .component = "transaction/lock-manager",
    .allocation_message = "deadlock graph allocation failed",
    .exception_prefix = "deadlock detection threw: ",
    .unknown_exception_message = "deadlock detection threw an unknown exception",
};

[[maybe_unused]] auto should_enqueue(
    [[maybe_unused]] const LockTarget& target,
    [[maybe_unused]] const LockState& state,
    [[maybe_unused]] base::Xid xid,
    [[maybe_unused]] std::uint8_t mode,
    [[maybe_unused]] bool upgrade
) -> base::Result<bool> {
    // LAB-S08.Q04:R19-BEGIN
    base::lab::missing_algorithm("S08.Q04:R19");
    // LAB-S08.Q04:R19-END
}

}  // namespace

[[maybe_unused]] auto LockManager::Inner::acquire_new_target(
    [[maybe_unused]] LockTargetShard& shard,
    [[maybe_unused]] const LockTarget& target,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::uint8_t mode,
    [[maybe_unused]] const bool upgrade,
    [[maybe_unused]] const bool transient
) -> base::Status {
    // LAB-S08.Q04:R20-BEGIN
    base::lab::missing_algorithm("S08.Q04:R20");
    // LAB-S08.Q04:R20-END
}

[[maybe_unused]] auto LockManager::Inner::try_immediate_grant(
    [[maybe_unused]] LockState& state,
    [[maybe_unused]] const LockTarget& target,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::uint8_t mode,
    [[maybe_unused]] const bool upgrade,
    [[maybe_unused]] const bool transient
) -> base::Result<bool> {
    // LAB-S08.Q04:R21-BEGIN
    base::lab::missing_algorithm("S08.Q04:R21");
    // LAB-S08.Q04:R21-END
}

[[maybe_unused]] auto LockManager::Inner::acquire(
    [[maybe_unused]] const LockTarget& target,
    [[maybe_unused]] base::Xid xid,
    [[maybe_unused]] std::uint8_t mode,
    [[maybe_unused]] bool upgrade,
    [[maybe_unused]] bool transient
) -> base::Status {
    // LAB-S08.Q04:R22-BEGIN
    base::lab::missing_algorithm("S08.Q04:R22");
    // LAB-S08.Q04:R22-END
}

[[maybe_unused]] auto LockManager::Inner::enqueue_and_wait(
    [[maybe_unused]] std::unique_lock<std::mutex>& lock,
    [[maybe_unused]] const LockTarget& target,
    [[maybe_unused]] LockState& state,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::uint8_t mode,
    [[maybe_unused]] const bool upgrade,
    [[maybe_unused]] const bool transient
) -> base::Status {
    // LAB-S08.Q04:R23-BEGIN
    base::lab::missing_algorithm("S08.Q04:R23");
    // LAB-S08.Q04:R23-END
}

[[maybe_unused]] auto
LockManager::Inner::check_deadlock([[maybe_unused]] const std::shared_ptr<LockRequest>& request)
    -> base::Status {
    // LAB-S08.Q05:R09-BEGIN
    base::lab::missing_algorithm("S08.Q05:R09");
    // LAB-S08.Q05:R09-END
}

[[maybe_unused]] auto LockManager::Inner::has_newer_cycle_waiter(
    [[maybe_unused]] const detail::LockWaitGraph& graph,
    [[maybe_unused]] const LockRequest& request
) const -> bool {
    // LAB-S08.Q05:R10-BEGIN
    base::lab::missing_algorithm("S08.Q05:R10");
    // LAB-S08.Q05:R10-END
}

[[maybe_unused]] void LockManager::Inner::grant_waiters([[maybe_unused]] LockState& state) {
    // LAB-S08.Q04:R24-BEGIN
    base::lab::missing_algorithm("S08.Q04:R24");
    // LAB-S08.Q04:R24-END
}

[[maybe_unused]] auto LockManager::Inner::record_immediate_grant(
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const LockTarget& target,
    [[maybe_unused]] const bool upgrade
) -> base::Status {
    // LAB-S08.Q04:R25-BEGIN
    base::lab::missing_algorithm("S08.Q04:R25");
    // LAB-S08.Q04:R25-END
}

[[maybe_unused]] auto LockManager::Inner::find_or_create_owner(
    [[maybe_unused]] LockOwnerShard& shard,
    [[maybe_unused]] const base::Xid xid
) -> std::pair<LockOwnerTable::iterator, bool> {
    // LAB-S08.Q04:R26-BEGIN
    base::lab::missing_algorithm("S08.Q04:R26");
    // LAB-S08.Q04:R26-END
}

[[maybe_unused]] void LockManager::Inner::recycle_lock_state(
    [[maybe_unused]] LockTargetShard& shard,
    [[maybe_unused]] const detail::LockTable::iterator state,
    [[maybe_unused]] detail::LockHolders::node_type holder
) {
    // LAB-S08.Q04:R27-BEGIN
    base::lab::missing_algorithm("S08.Q04:R27");
    // LAB-S08.Q04:R27-END
}

[[maybe_unused]] void LockManager::Inner::recycle_owner_state(
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] LockOwnerRelease& owner
) {
    // LAB-S08.Q04:R28-BEGIN
    base::lab::missing_algorithm("S08.Q04:R28");
    // LAB-S08.Q04:R28-END
}

[[maybe_unused]] void
LockManager::Inner::record_grant([[maybe_unused]] const LockRequest& request) {
    // LAB-S08.Q04:R29-BEGIN
    base::lab::missing_algorithm("S08.Q04:R29");
    // LAB-S08.Q04:R29-END
}

[[maybe_unused]] auto LockManager::Inner::wait_graph() const -> detail::LockWaitGraph {
    // LAB-S08.Q05:R11-BEGIN
    base::lab::missing_algorithm("S08.Q05:R11");
    // LAB-S08.Q05:R11-END
}

[[maybe_unused]] auto LockManager::Inner::lock_all_targets() const
    -> std::vector<std::unique_lock<std::mutex>> {
    // LAB-S08.Q05:R12-BEGIN
    base::lab::missing_algorithm("S08.Q05:R12");
    // LAB-S08.Q05:R12-END
}

[[maybe_unused]] void LockManager::Inner::remove_request(
    [[maybe_unused]] LockState& state,
    [[maybe_unused]] const std::shared_ptr<LockRequest>& request
) {
    // LAB-S08.Q05:R13-BEGIN
    base::lab::missing_algorithm("S08.Q05:R13");
    // LAB-S08.Q05:R13-END
}

[[maybe_unused]] void
LockManager::Inner::clear_waiting([[maybe_unused]] const LockRequest& request) {
    // LAB-S08.Q04:R30-BEGIN
    base::lab::missing_algorithm("S08.Q04:R30");
    // LAB-S08.Q04:R30-END
}

[[maybe_unused]] auto
LockManager::Inner::waiting_request([[maybe_unused]] const base::Xid xid) const
    -> std::shared_ptr<LockRequest> {
    // LAB-S08.Q04:R31-BEGIN
    base::lab::missing_algorithm("S08.Q04:R31");
    // LAB-S08.Q04:R31-END
}

[[maybe_unused]] auto
LockManager::Inner::cancel_request([[maybe_unused]] const std::shared_ptr<LockRequest>& request)
    -> base::Status {
    // LAB-S08.Q04:R32-BEGIN
    base::lab::missing_algorithm("S08.Q04:R32");
    // LAB-S08.Q04:R32-END
}

[[maybe_unused]] auto LockManager::Inner::cancel_wait([[maybe_unused]] base::Xid xid)
    -> base::Status {
    // LAB-S08.Q04:R33-BEGIN
    base::lab::missing_algorithm("S08.Q04:R33");
    // LAB-S08.Q04:R33-END
}

[[maybe_unused]] auto LockManager::Inner::begin_owner_release([[maybe_unused]] const base::Xid xid)
    -> LockOwnerRelease {
    // LAB-S08.Q04:R34-BEGIN
    base::lab::missing_algorithm("S08.Q04:R34");
    // LAB-S08.Q04:R34-END
}

[[maybe_unused]] auto LockManager::Inner::release_all([[maybe_unused]] base::Xid xid)
    -> base::Status {
    // LAB-S08.Q04:R35-BEGIN
    base::lab::missing_algorithm("S08.Q04:R35");
    // LAB-S08.Q04:R35-END
}

[[maybe_unused]] auto LockManager::Inner::finish_transaction([[maybe_unused]] const base::Xid xid)
    -> base::Status {
    // LAB-S08.Q04:R36-BEGIN
    base::lab::missing_algorithm("S08.Q04:R36");
    // LAB-S08.Q04:R36-END
}

void LockManager::Inner::shutdown() {
    shutting_down.store(true, std::memory_order_release);
    for (auto& shard : target_shards) {
        const std::lock_guard lock{shard.mutex};
        for (auto& entry : shard.locks) {
            for (const auto& request : entry.second.waiters) {
                request->cancelled = true;
                request->condition.notify_one();
            }
        }
        shard.locks.clear();
    }
    for (auto& shard : owner_shards) {
        const std::lock_guard lock{shard.mutex};
        shard.owners.clear();
    }
}

LockManager::LockManager()
    : LockManager{LockManagerOptions{}} {}

LockManager::LockManager(const LockManagerOptions& options)
    : inner_{std::make_unique<Inner>(options)} {}

LockManager::~LockManager() {
    inner_->shutdown();
}

[[maybe_unused]] auto LockManager::acquire_table(
    [[maybe_unused]] base::Xid xid,
    [[maybe_unused]] base::RelationId relation_id,
    [[maybe_unused]] TableLockMode mode
) -> base::Status {
    // LAB-S08.Q04:R37-BEGIN
    base::lab::missing_algorithm("S08.Q04:R37");
    // LAB-S08.Q04:R37-END
}

[[maybe_unused]] auto LockManager::upgrade_table(
    [[maybe_unused]] base::Xid xid,
    [[maybe_unused]] base::RelationId relation_id,
    [[maybe_unused]] TableLockMode mode
) -> base::Status {
    // LAB-S08.Q04:R38-BEGIN
    base::lab::missing_algorithm("S08.Q04:R38");
    // LAB-S08.Q04:R38-END
}

[[maybe_unused]] auto LockManager::acquire_row(
    [[maybe_unused]] base::Xid xid,
    [[maybe_unused]] base::RelationId relation_id,
    [[maybe_unused]] base::Rid rid,
    [[maybe_unused]] RowLockMode mode
) -> base::Status {
    // LAB-S08.Q04:R39-BEGIN
    base::lab::missing_algorithm("S08.Q04:R39");
    // LAB-S08.Q04:R39-END
}

[[maybe_unused]] auto LockManager::upgrade_row(
    [[maybe_unused]] base::Xid xid,
    [[maybe_unused]] base::RelationId relation_id,
    [[maybe_unused]] base::Rid rid,
    [[maybe_unused]] RowLockMode mode
) -> base::Status {
    // LAB-S08.Q04:R40-BEGIN
    base::lab::missing_algorithm("S08.Q04:R40");
    // LAB-S08.Q04:R40-END
}

[[maybe_unused]] auto LockManager::register_transaction([[maybe_unused]] const base::Xid xid)
    -> base::Status {
    // LAB-S08.Q04:R41-BEGIN
    base::lab::missing_algorithm("S08.Q04:R41");
    // LAB-S08.Q04:R41-END
}

[[maybe_unused]] auto LockManager::wait_for_transaction(
    [[maybe_unused]] const base::Xid waiter_xid,
    [[maybe_unused]] const base::Xid blocking_xid
) -> base::Status {
    // LAB-S08.Q04:R42-BEGIN
    base::lab::missing_algorithm("S08.Q04:R42");
    // LAB-S08.Q04:R42-END
}

[[maybe_unused]] auto LockManager::release_all([[maybe_unused]] base::Xid xid) -> base::Status {
    // LAB-S08.Q04:R43-BEGIN
    base::lab::missing_algorithm("S08.Q04:R43");
    // LAB-S08.Q04:R43-END
}

[[maybe_unused]] auto LockManager::finish_transaction([[maybe_unused]] base::Xid xid)
    -> base::Status {
    // LAB-S08.Q04:R44-BEGIN
    base::lab::missing_algorithm("S08.Q04:R44");
    // LAB-S08.Q04:R44-END
}

[[maybe_unused]] auto LockManager::cancel_wait([[maybe_unused]] base::Xid xid) -> base::Status {
    // LAB-S08.Q04:R45-BEGIN
    base::lab::missing_algorithm("S08.Q04:R45");
    // LAB-S08.Q04:R45-END
}

auto LockManager::held_table_mode(base::Xid xid, base::RelationId relation_id) const
    -> std::optional<TableLockMode> {
    const auto target = table_target(relation_id);
    const auto& shard = inner_->target_shard(target);
    const std::lock_guard lock{shard.mutex};
    const auto state = shard.locks.find(target);
    if (state == shard.locks.end()) {
        return std::nullopt;
    }
    const auto held = state->second.holders.find(xid);
    if (held == state->second.holders.end()) {
        return std::nullopt;
    }
    return static_cast<TableLockMode>(held->second);
}

auto LockManager::held_row_mode(base::Xid xid, base::RelationId relation_id, base::Rid rid) const
    -> std::optional<RowLockMode> {
    const auto target = row_target(relation_id, rid);
    const auto& shard = inner_->target_shard(target);
    const std::lock_guard lock{shard.mutex};
    const auto state = shard.locks.find(target);
    if (state == shard.locks.end()) {
        return std::nullopt;
    }
    const auto held = state->second.holders.find(xid);
    if (held == state->second.holders.end()) {
        return std::nullopt;
    }
    return static_cast<RowLockMode>(held->second);
}

auto LockManager::stats() const -> LockManagerStats {
    LockManagerStats result;
    for (const auto& shard : inner_->target_shards) {
        const std::lock_guard lock{shard.mutex};
        for (const auto& [target, state] : shard.locks) {
            switch (target.kind) {
                case LockTargetKind::kTable:
                    ++result.table_targets;
                    result.granted_locks += state.holders.size();
                    break;
                case LockTargetKind::kRow:
                    ++result.row_targets;
                    result.granted_locks += state.holders.size();
                    break;
                case LockTargetKind::kTransaction:
                    ++result.transaction_targets;
                    if (!state.waiters.empty()) {
                        ++result.transaction_wait_targets;
                        result.transaction_waiting_requests += state.waiters.size();
                    }
                    break;
            }
            result.waiting_requests += state.waiters.size();
        }
    }
    return result;
}

}  // namespace db::transaction
