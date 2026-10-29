#include "ssi_lifecycle.hpp"

namespace {

[[maybe_unused]] void compile_ssi_lifecycle_header() {
    db::transaction::detail::SsiStateMap states;
    db::transaction::detail::SsiTargetIndex index;
    db::transaction::detail::SsiPredicateIndex predicate_index;
    db::transaction::detail::ActiveSnapshotHorizon active_snapshots;
    db::transaction::detail::CommittedStateIndex committed_states;
    db::transaction::detail::prune_ssi_committed_states(
        states,
        index,
        predicate_index,
        active_snapshots,
        committed_states
    );
}

}  // namespace
