#include "lock_wait_graph.hpp"

namespace {

[[maybe_unused]] void compile_lock_wait_graph_header() {
    const db::transaction::detail::LockTable locks;
    static_cast<void>(db::transaction::detail::lock_wait_graph_has_cycle(locks, db::base::Xid{1}));
}

}  // namespace
