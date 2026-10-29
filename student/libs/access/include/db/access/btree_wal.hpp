#pragma once

#include <cstdint>
#include <span>

#include "db/access/btree_page_change.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/page.hpp"

namespace db::access {

enum class BtreeWalOperation : std::uint8_t {
    kInitializeMetadata = 0,
    kInsertLeaf = 1,
    kInsertUpper = 2,
    kSplit = 3,
    kDelete = 4,
    kNewRoot = 5,
    kVacuum = 6,
    kBuildPage = 7,
    kFinishSplit = 8,
    kSplitFinish = 9,
};

enum class BtreeWalPageRole : std::uint8_t {
    kPrimary = 0,
    kSplitLeft = 1,
    kSplitRight = 2,
    kSplitSibling = 3,
    kNewRoot = 4,
    kMetadata = 5,
    kBuildPage = 6,
    kSplitChild = 7,
};

// All pointers are borrowed only for the duration of log(). Every after page
// remains pinned and exclusively latched. A multi-page structural mutation is
// represented by one ordered span so its record can validate every block role.
struct BtreeWalPageMutation final {
    BtreeWalPageRole role{BtreeWalPageRole::kPrimary};
    storage::PageAddress address{base::kInvalidRelationId, base::kInvalidPageId};
    bool will_initialize{false};
    const storage::Page* before_page{nullptr};
    storage::Page* after_page{nullptr};
    // Non-empty for access-method operations that already know their exact
    // physical writes. Empty retains the generic delta path for page rewrites.
    std::span<const BtreePageChangeRange> changed_ranges{};
};

struct BtreeWalMutation final {
    BtreeWalOperation operation{BtreeWalOperation::kInsertLeaf};
    base::Xid xid{base::kInvalidXid};
    std::span<const BtreeWalPageMutation> pages{};
};

// Consumer-owned composition port. Success returns the common record end LSN;
// the access-method caller installs it while retaining every page latch.
// Failure restores every supplied page's exact before-image before returning.
class BtreeMutationLogPort {
public:
    BtreeMutationLogPort() = default;
    BtreeMutationLogPort(const BtreeMutationLogPort&) = delete;
    auto operator=(const BtreeMutationLogPort&) -> BtreeMutationLogPort& = delete;
    BtreeMutationLogPort(BtreeMutationLogPort&&) = delete;
    auto operator=(BtreeMutationLogPort&&) -> BtreeMutationLogPort& = delete;
    virtual ~BtreeMutationLogPort() = default;

    virtual auto log(const BtreeWalMutation& mutation) -> base::Result<base::Lsn> = 0;

    // See HeapMutationLogPort::writes_wal(). A WAL-free B+Tree build is only
    // valid before root publication and behind a caller-owned recovery
    // boundary.
    virtual auto writes_wal() const noexcept -> bool {
        return true;
    }
};

}  // namespace db::access
