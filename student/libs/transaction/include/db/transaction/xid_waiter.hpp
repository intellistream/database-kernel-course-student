#pragma once

#include <cstddef>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::transaction {

class LockManager;
class TransactionManager;

struct XidWaiterStats final {
    std::size_t blocking_transactions{0U};
    std::size_t waiting_transactions{0U};

    auto operator==(const XidWaiterStats&) const noexcept -> bool = default;
};

// Narrow XID-conflict facade over LockManager's private transaction targets.
// Only TransactionManager can create one, and its owned LockManager outlives
// the facade.
class XidWaiter final {
public:
    ~XidWaiter() = default;
    XidWaiter(const XidWaiter&) = delete;
    auto operator=(const XidWaiter&) -> XidWaiter& = delete;
    XidWaiter(XidWaiter&&) = delete;
    auto operator=(XidWaiter&&) -> XidWaiter& = delete;

    // Acquires a transient shared request on blocking_xid's transaction tag.
    // A completed transaction has no tag, so that request returns immediately.
    auto wait_for(base::Xid blocking_xid, base::Xid waiter_xid) const -> base::Status;

    auto stats() const -> XidWaiterStats;

private:
    friend class TransactionManager;

    explicit XidWaiter(LockManager& lock_manager) noexcept;

    LockManager* lock_manager_;
};

}  // namespace db::transaction
