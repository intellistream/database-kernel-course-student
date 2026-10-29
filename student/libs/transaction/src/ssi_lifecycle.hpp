#pragma once

#include <vector>

#include "db/base/identifier.hpp"

#include "ssi_state.hpp"

namespace db::transaction::detail {

// These functions require the owning SerializableConflictManager mutex. State
// removal preserves the compact outgoing-conflict ordering fact before it
// removes incident edges and physical SIREAD ownership.
void add_active_ssi_snapshot(ActiveSnapshotHorizon& horizon, std::uint64_t sequence);
void remove_active_ssi_snapshot(ActiveSnapshotHorizon& horizon, std::uint64_t sequence);
void doom_ssi_transaction(
    SsiStateMap& states,
    ActiveSnapshotHorizon& active_snapshots,
    base::Xid xid
);
void summarize_ssi_committed_outgoing(SsiStateMap& states, base::Xid xid);
void remove_ssi_transaction(
    SsiStateMap& states,
    SsiTargetIndex& index,
    SsiPredicateIndex& predicate_index,
    ActiveSnapshotHorizon& active_snapshots,
    CommittedStateIndex& committed_states,
    base::Xid xid
);
void remove_ssi_transactions(
    SsiStateMap& states,
    SsiTargetIndex& index,
    SsiPredicateIndex& predicate_index,
    ActiveSnapshotHorizon& active_snapshots,
    CommittedStateIndex& committed_states,
    const std::vector<base::Xid>& xids
);
void prune_ssi_committed_states(
    SsiStateMap& states,
    SsiTargetIndex& index,
    SsiPredicateIndex& predicate_index,
    ActiveSnapshotHorizon& active_snapshots,
    CommittedStateIndex& committed_states
);

}  // namespace db::transaction::detail
