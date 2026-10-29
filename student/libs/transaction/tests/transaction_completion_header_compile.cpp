#include "transaction_completion.hpp"

namespace {

[[maybe_unused]] void compile_transaction_completion_header(
    db::transaction::detail::TransactionCompletionServices services,
    db::transaction::ProcSlotLease& lease
) {
    const auto status = db::transaction::detail::publish_transaction_completion(
        services,
        lease,
        db::base::Xid{1},
        db::transaction::TransactionStatus::kCommitted,
        false
    );
    (void)status;
}

}  // namespace
