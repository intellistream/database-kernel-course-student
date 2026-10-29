#pragma once

#include "db/access/index_tid.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

namespace db::transaction {
class CommitLog;
class XidWaiter;
}  // namespace db::transaction

namespace db::access {

class HeapTable;

// Dirty-snapshot-like classifier shared by unique insertion and later index
// cleanup. oldest_xmin is captured by the caller once for the operation.
class HeapIndexTidClassifier final : public IndexTidClassifier, public UniqueConflictWaiter {
public:
    static auto create(
        const HeapTable& table,
        const transaction::CommitLog& commit_log,
        const transaction::XidWaiter& xid_waiter,
        base::Xid own_xid,
        base::Xid oldest_xmin
    ) -> base::Result<HeapIndexTidClassifier>;

    HeapIndexTidClassifier(const HeapIndexTidClassifier&) = delete;
    auto operator=(const HeapIndexTidClassifier&) -> HeapIndexTidClassifier& = delete;
    HeapIndexTidClassifier(HeapIndexTidClassifier&& other) noexcept;
    auto operator=(HeapIndexTidClassifier&& other) noexcept -> HeapIndexTidClassifier&;
    ~HeapIndexTidClassifier() override = default;

    // Moving transfers the complete operation-scoped borrow set and leaves
    // the source inert rather than duplicating a captured oldest-xmin view.
    auto valid() const noexcept -> bool;
    auto classify_tid(base::Rid rid) const -> base::Result<IndexTidState> override;
    auto wait_for(base::Xid blocking_xid) const -> base::Status override;

private:
    HeapIndexTidClassifier(
        const HeapTable& table,
        const transaction::CommitLog& commit_log,
        const transaction::XidWaiter& xid_waiter,
        base::Xid own_xid,
        base::Xid oldest_xmin
    ) noexcept;

    const HeapTable* table_;
    const transaction::CommitLog* commit_log_;
    const transaction::XidWaiter* xid_waiter_;
    base::Xid own_xid_;
    base::Xid oldest_xmin_;
};

}  // namespace db::access
