#include "ssi_lifecycle.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "db/base/lab_missing.hpp"

#include "ssi_read_locks.hpp"

namespace db::transaction::detail {
namespace {

[[maybe_unused]] void refresh_active_snapshot_horizon(
    [[maybe_unused]] const SsiStateMap& states,
    [[maybe_unused]] ActiveSnapshotHorizon& horizon
) {
    // LAB-S09.Q05:R09-BEGIN
    base::lab::missing_algorithm("S09.Q05:R09");
    // LAB-S09.Q05:R09-END
}

[[maybe_unused]] void remove_predicate_lock_holder(
    [[maybe_unused]] SsiPredicateIndex& index,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const base::Xid xid
) {
    // LAB-S09.Q05:R10-BEGIN
    base::lab::missing_algorithm("S09.Q05:R10");
    // LAB-S09.Q05:R10-END
}
}  // namespace

[[maybe_unused]] void add_active_ssi_snapshot(
    [[maybe_unused]] ActiveSnapshotHorizon& horizon,
    [[maybe_unused]] const std::uint64_t sequence
) {
    // LAB-S09.Q06:R33-BEGIN
    base::lab::missing_algorithm("S09.Q06:R33");
    // LAB-S09.Q06:R33-END
}

[[maybe_unused]] void remove_active_ssi_snapshot(
    [[maybe_unused]] ActiveSnapshotHorizon& horizon,
    [[maybe_unused]] const std::uint64_t sequence
) {
    // LAB-S09.Q06:R34-BEGIN
    base::lab::missing_algorithm("S09.Q06:R34");
    // LAB-S09.Q06:R34-END
}

[[maybe_unused]] void doom_ssi_transaction(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] ActiveSnapshotHorizon& active_snapshots,
    [[maybe_unused]] const base::Xid xid
) {
    // LAB-S09.Q06:R35-BEGIN
    base::lab::missing_algorithm("S09.Q06:R35");
    // LAB-S09.Q06:R35-END
}

[[maybe_unused]] void summarize_ssi_committed_outgoing(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] const base::Xid xid
) {
    // LAB-S09.Q06:R36-BEGIN
    base::lab::missing_algorithm("S09.Q06:R36");
    // LAB-S09.Q06:R36-END
}

[[maybe_unused]] void remove_ssi_transaction(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] SsiTargetIndex& index,
    [[maybe_unused]] SsiPredicateIndex& predicate_index,
    [[maybe_unused]] ActiveSnapshotHorizon& active_snapshots,
    [[maybe_unused]] CommittedStateIndex& committed_states,
    [[maybe_unused]] const base::Xid xid
) {
    // LAB-S09.Q06:R37-BEGIN
    base::lab::missing_algorithm("S09.Q06:R37");
    // LAB-S09.Q06:R37-END
}

[[maybe_unused]] void remove_ssi_transactions(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] SsiTargetIndex& index,
    [[maybe_unused]] SsiPredicateIndex& predicate_index,
    [[maybe_unused]] ActiveSnapshotHorizon& active_snapshots,
    [[maybe_unused]] CommittedStateIndex& committed_states,
    [[maybe_unused]] const std::vector<base::Xid>& xids
) {
    // LAB-S09.Q06:R38-BEGIN
    base::lab::missing_algorithm("S09.Q06:R38");
    // LAB-S09.Q06:R38-END
}

[[maybe_unused]] void prune_ssi_committed_states(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] SsiTargetIndex& index,
    [[maybe_unused]] SsiPredicateIndex& predicate_index,
    [[maybe_unused]] ActiveSnapshotHorizon& active_snapshots,
    [[maybe_unused]] CommittedStateIndex& committed_states
) {
    // LAB-S09.Q06:R39-BEGIN
    base::lab::missing_algorithm("S09.Q06:R39");
    // LAB-S09.Q06:R39-END
}
}  // namespace db::transaction::detail
