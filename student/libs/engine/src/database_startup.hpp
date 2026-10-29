#pragma once

#include <cstdint>
#include <memory>

#include "db/catalog/store.hpp"
#include "db/engine/database.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/manager.hpp"
#include "db/wal/control_store.hpp"
#include "db/wal/manager.hpp"

namespace db::engine::detail {

// Opening helpers return owners to the database composition root. Recovery borrows
// them synchronously and releases all replay views before normal services start.
enum class ControlOpenMode : std::uint8_t {
    kCreate = 0U,
    kOpen = 1U,
    kOpenOrCreate = 2U,
};

struct ControlBootstrap final {
    wal::WalControlStore control;
    std::uint64_t system_identifier{0U};
    bool created{false};
};

auto validate_config(const DatabaseConfig& config) -> base::Status;

auto publish_startup_state(wal::WalControlStore& control, wal::DatabaseState state) -> base::Status;

auto open_control(const DatabaseConfig& config, ControlOpenMode mode)
    -> base::Result<ControlBootstrap>;

auto open_wal(
    const DatabaseConfig& config,
    bool create_database,
    const wal::WalControlImage& control,
    std::uint64_t system_identifier
) -> base::Result<std::shared_ptr<wal::WalManager>>;

auto open_commit_log(
    const DatabaseConfig& config,
    bool create_database,
    std::uint64_t system_identifier
) -> base::Result<std::shared_ptr<transaction::CommitLog>>;

auto open_transaction_manager(
    transaction::TransactionStartupState startup_state,
    const std::shared_ptr<wal::WalManager>& wal_manager
) -> base::Result<std::unique_ptr<transaction::TransactionManager>>;

auto open_catalog(const DatabaseConfig& config, bool create_database)
    -> base::Result<catalog::CatalogStore>;

auto open_relation_disk(const DatabaseConfig& config)
    -> base::Result<std::unique_ptr<storage::DiskManager>>;

auto open_buffer_pool(
    const DatabaseConfig& config,
    std::unique_ptr<storage::DiskManager> disk,
    const std::shared_ptr<wal::WalManager>& wal_manager
) -> base::Result<std::unique_ptr<storage::BufferPool>>;

auto recover_startup_if_required(
    const DatabaseConfig& config,
    bool requires_replay,
    wal::WalControlStore& control,
    const std::shared_ptr<wal::WalManager>& wal_manager,
    const std::shared_ptr<transaction::CommitLog>& commit_log,
    catalog::CatalogStore& catalog,
    storage::DiskManager& disk
) -> base::Result<transaction::TransactionStartupState>;

}  // namespace db::engine::detail
