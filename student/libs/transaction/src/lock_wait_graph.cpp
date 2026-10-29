#include "lock_wait_graph.hpp"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "db/base/lab_missing.hpp"

#include "lock_mode_policy.hpp"

namespace db::transaction::detail {
namespace {

[[maybe_unused]] void add_wait_edge(
    [[maybe_unused]] LockWaitGraph& graph,
    [[maybe_unused]] base::Xid waiter,
    [[maybe_unused]] base::Xid blocker
) {
    // LAB-S08.Q05:R01-BEGIN
    base::lab::missing_algorithm("S08.Q05:R01");
    // LAB-S08.Q05:R01-END
}

[[maybe_unused]] auto reaches_xid(
    [[maybe_unused]] const LockWaitGraph& graph,
    [[maybe_unused]] base::Xid start,
    [[maybe_unused]] base::Xid target
) -> bool {
    // LAB-S08.Q05:R02-BEGIN
    base::lab::missing_algorithm("S08.Q05:R02");
    // LAB-S08.Q05:R02-END
}

[[maybe_unused]] void append_holder_wait_edges(
    [[maybe_unused]] LockWaitGraph& graph,
    [[maybe_unused]] const LockTarget& target,
    [[maybe_unused]] const LockState& state,
    [[maybe_unused]] const LockRequest& request
) {
    // LAB-S08.Q05:R03-BEGIN
    base::lab::missing_algorithm("S08.Q05:R03");
    // LAB-S08.Q05:R03-END
}

[[maybe_unused]] void
append_wait_edges([[maybe_unused]] LockWaitGraph& graph, [[maybe_unused]] const LockTable& locks) {
    // LAB-S08.Q05:R04-BEGIN
    base::lab::missing_algorithm("S08.Q05:R04");
    // LAB-S08.Q05:R04-END
}

}  // namespace

[[maybe_unused]] auto lock_wait_graph_has_cycle(
    [[maybe_unused]] const LockTable& locks,
    [[maybe_unused]] base::Xid requester
) -> bool {
    // LAB-S08.Q05:R05-BEGIN
    base::lab::missing_algorithm("S08.Q05:R05");
    // LAB-S08.Q05:R05-END
}

[[maybe_unused]] void append_lock_wait_graph(
    [[maybe_unused]] LockWaitGraph& graph,
    [[maybe_unused]] const LockTable& locks
) {
    // LAB-S08.Q05:R06-BEGIN
    base::lab::missing_algorithm("S08.Q05:R06");
    // LAB-S08.Q05:R06-END
}

[[maybe_unused]] auto lock_wait_graph_path_exists(
    [[maybe_unused]] const LockWaitGraph& graph,
    [[maybe_unused]] const base::Xid from,
    [[maybe_unused]] const base::Xid to
) -> bool {
    // LAB-S08.Q05:R07-BEGIN
    base::lab::missing_algorithm("S08.Q05:R07");
    // LAB-S08.Q05:R07-END
}

[[maybe_unused]] auto lock_wait_graph_same_cycle(
    [[maybe_unused]] const LockWaitGraph& graph,
    [[maybe_unused]] const base::Xid left,
    [[maybe_unused]] const base::Xid right
) -> bool {
    // LAB-S08.Q05:R08-BEGIN
    base::lab::missing_algorithm("S08.Q05:R08");
    // LAB-S08.Q05:R08-END
}

}  // namespace db::transaction::detail
