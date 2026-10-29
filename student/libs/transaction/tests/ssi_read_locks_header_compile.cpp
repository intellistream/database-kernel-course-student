#include "ssi_read_locks.hpp"

namespace {

[[maybe_unused]] void compile_ssi_read_locks_header() {
    db::transaction::detail::SsiStateMap states;
    db::transaction::detail::SsiTargetIndex index;
    db::transaction::detail::rebuild_read_lock_index(states, index);
}

}  // namespace
