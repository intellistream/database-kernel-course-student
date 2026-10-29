#include "ssi_conflict_graph.hpp"

namespace {

[[maybe_unused]] void compile_ssi_conflict_graph_header() {
    db::transaction::detail::SsiStateMap states;
    db::transaction::detail::ActiveSnapshotHorizon active_snapshots;
    db::transaction::detail::flag_ssi_rw_conflict(
        states,
        active_snapshots,
        db::base::Xid{1},
        db::base::Xid{2},
        db::base::Xid{1}
    );
}

}  // namespace
