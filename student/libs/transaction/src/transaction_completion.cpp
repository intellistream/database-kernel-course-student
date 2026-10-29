#include "transaction_completion.hpp"

#include <chrono>
#include <shared_mutex>
#include <utility>

#include "db/base/lab_missing.hpp"
#include "db/transaction/manager.hpp"

#include "completion_journal_adapter.hpp"
#include "xid_assignment.hpp"

namespace db::transaction::detail {

[[maybe_unused]] auto TransactionCompletionAccess::set_status_preallocated(
    [[maybe_unused]] CommitLog& commit_log,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const TransactionStatus status
) -> base::Status {
    // LAB-S08.Q06:R01-BEGIN
    base::lab::missing_algorithm("S08.Q06:R01");
    // LAB-S08.Q06:R01-END
}

[[maybe_unused]] auto TransactionCompletionAccess::assign_xid_prepared(
    [[maybe_unused]] ProcArray& proc_array,
    [[maybe_unused]] ProcSlotLease& lease,
    [[maybe_unused]] XidAssignmentPreparation& preparation
) -> base::Result<base::Xid> {
    // LAB-S08.Q06:R02-BEGIN
    base::lab::missing_algorithm("S08.Q06:R02");
    // LAB-S08.Q06:R02-END
}

[[maybe_unused]] auto TransactionCompletionAccess::register_transaction(
    [[maybe_unused]] LockManager& lock_manager,
    [[maybe_unused]] const base::Xid xid
) -> base::Status {
    // LAB-S08.Q06:R03-BEGIN
    base::lab::missing_algorithm("S08.Q06:R03");
    // LAB-S08.Q06:R03-END
}

[[maybe_unused]] auto TransactionCompletionAccess::publish_prevalidated_completion(
    [[maybe_unused]] ProcArray& proc_array,
    [[maybe_unused]] ProcSlotLease& lease,
    [[maybe_unused]] const base::Xid expected_xid
) -> base::Status {
    // LAB-S08.Q06:R04-BEGIN
    base::lab::missing_algorithm("S08.Q06:R04");
    // LAB-S08.Q06:R04-END
}

[[maybe_unused]] auto TransactionCompletionAccess::finish_transaction(
    [[maybe_unused]] LockManager& lock_manager,
    [[maybe_unused]] const base::Xid xid
) -> base::Status {
    // LAB-S08.Q06:R05-BEGIN
    base::lab::missing_algorithm("S08.Q06:R05");
    // LAB-S08.Q06:R05-END
}

namespace {

[[maybe_unused]] auto publish_completion_suffix(
    [[maybe_unused]] TransactionCompletionServices services,
    [[maybe_unused]] ProcSlotLease& lease,
    [[maybe_unused]] base::Xid expected_xid,
    [[maybe_unused]] TransactionStatus final_status
) -> base::Status {
    // LAB-S08.Q06:R06-BEGIN
    base::lab::missing_algorithm("S08.Q06:R06");
    // LAB-S08.Q06:R06-END
}

}  // namespace

[[maybe_unused]] auto publish_transaction_completion(
    [[maybe_unused]] const TransactionCompletionServices services,
    [[maybe_unused]] ProcSlotLease& lease,
    [[maybe_unused]] const base::Xid expected_xid,
    [[maybe_unused]] const TransactionStatus final_status,
    [[maybe_unused]] const bool requires_completion_journal
) -> base::Status {
    // LAB-S08.Q06:R07-BEGIN
    base::lab::missing_algorithm("S08.Q06:R07");
    // LAB-S08.Q06:R07-END
}

[[maybe_unused]] auto publish_abandoned_transaction_abort(
    [[maybe_unused]] const TransactionCompletionServices services,
    [[maybe_unused]] SerializableConflictManager& ssi_manager,
    [[maybe_unused]] const base::Xid xid
) -> base::Status {
    // LAB-S08.Q06:R08-BEGIN
    base::lab::missing_algorithm("S08.Q06:R08");
    // LAB-S08.Q06:R08-END
}

}  // namespace db::transaction::detail
