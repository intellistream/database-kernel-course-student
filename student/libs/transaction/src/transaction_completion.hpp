#pragma once

#include <shared_mutex>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/completion_journal.hpp"
#include "db/transaction/lock_manager.hpp"
#include "db/transaction/proc_array.hpp"
#include "db/transaction/ssi.hpp"

namespace db::transaction::detail {

class XidAssignmentPreparation;

class TransactionCompletionAccess final {
public:
    static auto
    set_status_preallocated(CommitLog& commit_log, base::Xid xid, TransactionStatus status)
        -> base::Status;
    static auto assign_xid_prepared(
        ProcArray& proc_array,
        ProcSlotLease& lease,
        XidAssignmentPreparation& preparation
    ) -> base::Result<base::Xid>;
    static auto register_transaction(LockManager& lock_manager, base::Xid xid) -> base::Status;
    static auto publish_prevalidated_completion(
        ProcArray& proc_array,
        ProcSlotLease& lease,
        base::Xid expected_xid
    ) -> base::Status;
    static auto finish_transaction(LockManager& lock_manager, base::Xid xid) -> base::Status;
};

struct TransactionCompletionServices final {
    CommitLog& commit_log;
    ProcArray& proc_array;
    LockManager& lock_manager;
    CompletionJournalPort* completion_journal;
    std::shared_mutex& completion_gate;
};

auto publish_transaction_completion(
    TransactionCompletionServices services,
    ProcSlotLease& lease,
    base::Xid expected_xid,
    TransactionStatus final_status,
    bool requires_completion_journal
) -> base::Status;

auto publish_abandoned_transaction_abort(
    TransactionCompletionServices services,
    SerializableConflictManager& ssi_manager,
    base::Xid xid
) -> base::Status;

}  // namespace db::transaction::detail
