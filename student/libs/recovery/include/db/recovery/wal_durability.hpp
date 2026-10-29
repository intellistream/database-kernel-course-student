#pragma once

#include <memory>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/durability.hpp"

namespace db::wal {
class WalManager;
}  // namespace db::wal

namespace db::recovery {

// Composition-layer implementation of storage's WAL-before-data barrier. The
// shared-owner construction retains the manager so a BufferPool cannot outlive
// its provider. Borrowed construction is available to scopes whose destruction
// order already guarantees that the manager outlives the adapter.
class WalPageDurabilityAdapter final : public storage::PageDurabilityPort {
public:
    explicit WalPageDurabilityAdapter(wal::WalManager& manager) noexcept;
    explicit WalPageDurabilityAdapter(std::shared_ptr<wal::WalManager> manager) noexcept;

    auto flush_through(base::Lsn target_lsn) -> base::Status override;

private:
    std::shared_ptr<wal::WalManager> owner_;
    wal::WalManager* manager_{nullptr};
};

}  // namespace db::recovery
