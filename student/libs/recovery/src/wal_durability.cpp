#include "db/recovery/wal_durability.hpp"

#include <utility>

#include "db/base/error.hpp"
#include "db/wal/manager.hpp"

namespace db::recovery {

WalPageDurabilityAdapter::WalPageDurabilityAdapter(wal::WalManager& manager) noexcept
    : manager_{&manager} {}

WalPageDurabilityAdapter::WalPageDurabilityAdapter(
    std::shared_ptr<wal::WalManager> manager
) noexcept
    : owner_{std::move(manager)},
      manager_{owner_.get()} {}

auto WalPageDurabilityAdapter::flush_through(const base::Lsn target_lsn) -> base::Status {
    if (manager_ == nullptr) {
        return base::fail(
            base::Error{base::ErrorCode::kUnavailable, "WAL durability manager is unavailable"}
                .with_context("recovery.wal_durability", "flush page WAL")
        );
    }
    if (auto status = manager_->flush_through(target_lsn); !status) {
        return base::fail(
            std::move(status).error().with_context("recovery.wal_durability", "flush page WAL")
        );
    }
    return {};
}

}  // namespace db::recovery
