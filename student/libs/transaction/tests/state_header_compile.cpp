#include "db/transaction/state.hpp"

namespace {

[[maybe_unused]] void compile_transaction_state_header() {
    db::transaction::TransactionStateMachine state;
    (void)state.state();
}

}  // namespace
