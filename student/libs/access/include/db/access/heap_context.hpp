#pragma once

#include "db/base/identifier.hpp"

namespace db::transaction {

class CommitLog;
class SerializableConflictManager;
struct Snapshot;

}  // namespace db::transaction

namespace db::access {

// Common borrowed transaction view for heap reads and mutations. Every
// referenced service must outlive the enclosing access-method call or scan.
// Keeping this bundle explicit gives all heap entry points one MVCC/SSI
// boundary instead of duplicating an evolving list of transaction services.
struct HeapMvccContext final {
    base::Xid own_xid{base::kInvalidXid};
    const transaction::Snapshot* snapshot{nullptr};
    const transaction::CommitLog* commit_log{nullptr};
    transaction::SerializableConflictManager* serializable_conflicts{nullptr};
};

}  // namespace db::access
