#pragma once

#include <cstdint>

#include "db/base/identifier.hpp"

namespace db::wal {

// Stable semantic states persisted by the WAL control image. Durable encoding
// and validation belong to the WAL format layer rather than this value contract.
enum class DatabaseState : std::uint8_t {
    kShutdown = 0,
    kStartingUp = 1,
    kInProduction = 2,
    kInCrashRecovery = 3,
    kShuttingDown = 4,
};

struct WalControlImage final {
    std::uint64_t system_identifier{0};
    std::uint64_t generation{0};
    base::Lsn checkpoint_lsn{base::kInvalidLsn};
    base::Lsn redo_lsn{base::kInvalidLsn};
    base::Xid next_xid{base::Xid{1}};
    base::Xid latest_completed_xid{base::kInvalidXid};
    std::uint32_t next_object_id{1};
    DatabaseState state{DatabaseState::kShutdown};
    std::uint32_t checksum{0};

    auto operator==(const WalControlImage&) const noexcept -> bool = default;
};

}  // namespace db::wal
