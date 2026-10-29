#pragma once

#include <cstdint>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

#include "ssi_state.hpp"

namespace db::transaction::detail {

// These functions require the owning SerializableConflictManager mutex.
auto advance_ssi_commit_sequence(std::uint64_t& last_sequence) -> base::Result<std::uint64_t>;
auto prepare_ssi_transaction(
    SsiStateMap& states,
    ActiveSnapshotHorizon& active_snapshots,
    std::uint64_t& last_commit_sequence,
    base::Xid xid
) -> base::Status;

}  // namespace db::transaction::detail
