#pragma once

#include <condition_variable>
#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "db/base/identifier.hpp"

#include "lock_mode_policy.hpp"

namespace db::transaction::detail {

struct LockTarget final {
    LockTargetKind kind{LockTargetKind::kTable};
    base::RelationId relation_id{base::kInvalidRelationId};
    base::Rid rid{base::kInvalidRid};
    base::Xid transaction_xid{base::kInvalidXid};

    auto operator==(const LockTarget&) const -> bool = default;
};

struct LockTargetHash final {
    auto operator()(const LockTarget& target) const noexcept -> std::size_t {
        switch (target.kind) {
            case LockTargetKind::kTable:
                return target.relation_id.value();
            case LockTargetKind::kRow: {
                auto value = (static_cast<std::uint64_t>(target.relation_id.value()) << 32U)
                             | target.rid.page_id().value();
                value ^= static_cast<std::uint64_t>(target.rid.slot_id().value())
                         * 0x9e37'79b9'7f4a'7c15ULL;
                return value ^ (value >> 29U);
            }
            case LockTargetKind::kTransaction:
                return target.transaction_xid.value();
        }
        std::unreachable();
    }
};

// Keep the three locktag domains in separate partition ranges. Table locks
// preserve relation locality, while tuple locks spread pages and slots across
// twice as many partitions without colliding with their table intent lock.
inline constexpr std::size_t kTableLockShardBase = 0U;
inline constexpr std::size_t kTableLockShardCount = 16U;
inline constexpr std::size_t kTransactionLockShardBase = 16U;
inline constexpr std::size_t kTransactionLockShardCount = 16U;
inline constexpr std::size_t kRowLockShardBase = 32U;
inline constexpr std::size_t kRowLockShardCount = 64U;
inline constexpr std::size_t kLockTargetShardCount = 96U;

inline auto lock_target_shard_index(const LockTarget& target) noexcept -> std::size_t {
    switch (target.kind) {
        case LockTargetKind::kTable:
            return kTableLockShardBase + (target.relation_id.value() & (kTableLockShardCount - 1U));
        case LockTargetKind::kRow: {
            const auto value = static_cast<std::uint64_t>(target.relation_id.value())
                               + static_cast<std::uint64_t>(target.rid.page_id().value()) * 33U
                               + static_cast<std::uint64_t>(target.rid.slot_id().value()) * 17U;
            return kRowLockShardBase + (value & (kRowLockShardCount - 1U));
        }
        case LockTargetKind::kTransaction:
            return kTransactionLockShardBase
                   + (target.transaction_xid.value() & (kTransactionLockShardCount - 1U));
    }
    std::unreachable();
}

using LockHolders = std::unordered_map<base::Xid, std::uint8_t>;

inline constexpr std::uint8_t kTransactionWaitMode = 0U;
inline constexpr std::uint8_t kTransactionRunningMode = 1U;

struct LockRequest final {
    base::Xid xid{base::kInvalidXid};
    LockTarget target;
    std::uint64_t sequence{0};
    std::uint8_t mode{0};
    bool upgrade{false};
    bool transient{false};
    bool granted{false};
    bool cancelled{false};
    bool deadlock_victim{false};
    LockHolders::node_type prepared_holder;
    std::condition_variable condition;
};

struct LockState final {
    LockHolders holders;
    std::list<std::shared_ptr<LockRequest>> waiters;
    base::Xid transaction_owner{base::kInvalidXid};
};

using LockTable = std::unordered_map<LockTarget, LockState, LockTargetHash>;
using GrantedLockOwners = std::unordered_map<base::Xid, std::vector<LockTarget>>;
using WaitingLockOwners = std::unordered_map<base::Xid, std::shared_ptr<LockRequest>>;

}  // namespace db::transaction::detail
