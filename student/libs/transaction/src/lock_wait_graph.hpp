#pragma once

#include <unordered_map>
#include <vector>

#include "db/base/identifier.hpp"

#include "lock_manager_state.hpp"

namespace db::transaction::detail {

using LockWaitGraph = std::unordered_map<base::Xid, std::vector<base::Xid>>;

// The caller owns synchronization for the complete lock table snapshot.
auto lock_wait_graph_has_cycle(const LockTable& locks, base::Xid requester) -> bool;
void append_lock_wait_graph(LockWaitGraph& graph, const LockTable& locks);
auto lock_wait_graph_path_exists(const LockWaitGraph& graph, base::Xid from, base::Xid to) -> bool;
auto lock_wait_graph_same_cycle(const LockWaitGraph& graph, base::Xid left, base::Xid right)
    -> bool;

}  // namespace db::transaction::detail
