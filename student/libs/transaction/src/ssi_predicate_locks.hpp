#pragma once

#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"

#include "ssi_state.hpp"

namespace db::transaction::detail {

// Collection and revalidation require the owning manager mutex. Predicate
// evaluation happens between these phases without that mutex.
auto has_all_ssi_predicate_lock(
    const SsiTransactionState& state,
    base::RelationId relation_id
) noexcept -> bool;
auto collect_ssi_predicate_candidates(
    SsiStateMap& states,
    const SsiPredicateIndex& predicate_index,
    base::Xid writer_xid,
    base::RelationId relation_id
) -> base::Result<std::vector<PredicateCandidate>>;
auto revalidate_ssi_predicate_readers(
    const SsiStateMap& states,
    base::Xid writer_xid,
    const std::vector<PredicateCandidate>& matches
) -> std::vector<base::Xid>;

}  // namespace db::transaction::detail
