#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "db/access/btree_wal.hpp"
#include "db/base/error.hpp"
#include "db/storage/durability.hpp"

namespace db::access::test {

class MonotonicBtreeLog final : public BtreeMutationLogPort {
public:
    auto log(const BtreeWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.pages.empty()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "test B-tree log received an invalid mutation",
                }
            );
        }
        return base::Lsn{next_lsn_.fetch_add(1U, std::memory_order_relaxed)};
    }

private:
    std::atomic<std::uint64_t> next_lsn_{1U};
};

inline auto btree_log() -> BtreeMutationLogPort& {
    static MonotonicBtreeLog log;
    return log;
}

class ImmediateDurability final : public storage::PageDurabilityPort {
public:
    auto flush_through(base::Lsn /*target_lsn*/) -> base::Status override {
        return {};
    }
};

inline auto btree_durability() -> std::unique_ptr<storage::PageDurabilityPort> {
    return std::make_unique<ImmediateDurability>();
}

}  // namespace db::access::test
