#pragma once

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::storage {

// Consumer-owned port implemented by the composition layer. Storage declares
// the ordering it needs without depending on a concrete WAL component.
class PageDurabilityPort {
public:
    PageDurabilityPort() = default;
    PageDurabilityPort(const PageDurabilityPort&) = delete;
    auto operator=(const PageDurabilityPort&) -> PageDurabilityPort& = delete;
    PageDurabilityPort(PageDurabilityPort&&) = delete;
    auto operator=(PageDurabilityPort&&) -> PageDurabilityPort& = delete;
    virtual ~PageDurabilityPort() = default;

    // On success, WAL through target_lsn must be durable before this call
    // returns. Implementations report failure through Status rather than
    // throwing and must treat repeated or older targets as safe. BufferPool
    // defensively converts an escaped exception and keeps the page dirty.
    virtual auto flush_through(base::Lsn target_lsn) -> base::Status = 0;
};

}  // namespace db::storage
