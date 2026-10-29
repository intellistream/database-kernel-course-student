#pragma once

#include <cstdint>
#include <memory>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/page.hpp"

namespace db::recovery {

// Whether recovery must obtain a validated current page before applying a
// record. A full-page image or WILL_INIT record replaces the old contents, so
// reading a torn target would be both unnecessary and unsafe.
enum class StartupPageLoadMode : std::uint8_t {
    kReadExisting,
    kSkipExisting,
};

// Request made only after RecoveryDispatcher has accepted a page record. The
// access implementation owns relation discovery, allocation, caching, pins,
// and latches; recovery supplies no normal buffer-pool object here.
struct StartupPageRequest final {
    storage::PageAddress address{base::kInvalidRelationId, base::kInvalidPageId};
    storage::PageKind expected_kind{storage::PageKind::kUninitialized};
    StartupPageLoadMode load_mode{StartupPageLoadMode::kReadExisting};

    auto operator==(const StartupPageRequest&) const noexcept -> bool = default;
};

// Move-only ownership is represented by the returned unique_ptr. A lease
// grants exclusive mutable access until destruction; no view obtained from
// page() may outlive it. mark_dirty() records a successful logical change but
// is not a durability point: the recovery page cache owns eviction, final
// drain, and relation sync.
class StartupPageLease {
public:
    StartupPageLease(const StartupPageLease&) = delete;
    auto operator=(const StartupPageLease&) -> StartupPageLease& = delete;
    StartupPageLease(StartupPageLease&&) = delete;
    auto operator=(StartupPageLease&&) -> StartupPageLease& = delete;
    virtual ~StartupPageLease() = default;

    virtual auto page() noexcept -> storage::Page& = 0;
    virtual auto mark_dirty() -> base::Status = 0;

protected:
    StartupPageLease() = default;
};

// Recovery-owned consumer port. It is intentionally narrower than DiskManager
// and BufferPool so page-local rmgr replay can be tested independently and the
// bounded recovery cache can supply the same lease contract.
class StartupPageAccessPort {
public:
    StartupPageAccessPort() = default;
    StartupPageAccessPort(const StartupPageAccessPort&) = delete;
    auto operator=(const StartupPageAccessPort&) -> StartupPageAccessPort& = delete;
    StartupPageAccessPort(StartupPageAccessPort&&) = delete;
    auto operator=(StartupPageAccessPort&&) -> StartupPageAccessPort& = delete;
    virtual ~StartupPageAccessPort() = default;

    virtual auto acquire(StartupPageRequest request)
        -> base::Result<std::unique_ptr<StartupPageLease>> = 0;
};

}  // namespace db::recovery
