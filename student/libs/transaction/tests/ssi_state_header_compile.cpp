#include "ssi_state.hpp"

namespace {

[[maybe_unused]] void compile_ssi_state_header() {
    db::transaction::detail::SsiStateMap states;
    states.emplace(db::base::Xid{1}, db::transaction::detail::SsiTransactionState{1});
}

}  // namespace
