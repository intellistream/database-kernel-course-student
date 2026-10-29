#pragma once

#include "db/base/identifier.hpp"
#include "db/transaction/ssi.hpp"

#include "ssi_state.hpp"

namespace db::transaction::detail {

// All mutations require the owning SerializableConflictManager mutex. The
// state map and target index must describe the same physical SIREAD owners.
auto has_relation_read_lock(const SsiTransactionState& state, base::RelationId relation_id) noexcept
    -> bool;
auto has_page_read_lock(
    const SsiTransactionState& state,
    base::RelationId relation_id,
    base::PageId page_id
) noexcept -> bool;
auto has_exact_read_lock(const SsiTransactionState& state, const SsiReadTarget& target) noexcept
    -> bool;
void remove_read_lock_holder(SsiTargetIndex& index, const SsiReadTarget& target, base::Xid xid);
void add_read_lock_holder(SsiTargetIndex& index, const SsiReadTarget& target, base::Xid xid);
void rebuild_read_lock_index(const SsiStateMap& states, SsiTargetIndex& index);
void promote_read_locks_to_relation(
    SsiStateMap& states,
    SsiTargetIndex& index,
    base::Xid reader_xid,
    base::RelationId relation_id
);
void promote_read_locks_to_page(
    SsiStateMap& states,
    SsiTargetIndex& index,
    base::Xid reader_xid,
    base::RelationId relation_id,
    base::PageId page_id
);

}  // namespace db::transaction::detail
