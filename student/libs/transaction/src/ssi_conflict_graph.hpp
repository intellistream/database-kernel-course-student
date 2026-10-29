#pragma once

#include "db/base/identifier.hpp"

#include "ssi_state.hpp"

namespace db::transaction::detail {

// These functions require the owning SerializableConflictManager mutex. Edge
// insertion keeps both endpoint lists symmetric and applies victim selection.
auto ssi_reader_can_conflict_with_writer(
    base::Xid reader_xid,
    const SsiTransactionState& reader,
    const SsiTransactionState& writer
) noexcept -> bool;
void flag_ssi_rw_conflict(
    SsiStateMap& states,
    ActiveSnapshotHorizon& active_snapshots,
    base::Xid reader_xid,
    base::Xid writer_xid,
    base::Xid actor_xid
);
void record_ssi_out_conflict_if_concurrent(
    SsiStateMap& states,
    ActiveSnapshotHorizon& active_snapshots,
    base::Xid reader_xid,
    base::Xid writer_xid
);

}  // namespace db::transaction::detail
