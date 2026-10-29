#include "ssi_read_locks.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "db/base/lab_missing.hpp"

namespace db::transaction::detail {
[[maybe_unused]] void remove_read_lock_holder(
    [[maybe_unused]] SsiTargetIndex& index,
    [[maybe_unused]] const SsiReadTarget& target,
    [[maybe_unused]] const base::Xid xid
) {
    // LAB-S09.Q05:R14-BEGIN
    base::lab::missing_algorithm("S09.Q05:R14");
    // LAB-S09.Q05:R14-END
}

[[maybe_unused]] auto has_relation_read_lock(
    [[maybe_unused]] const SsiTransactionState& state,
    [[maybe_unused]] const base::RelationId relation_id
) noexcept -> bool {
    // LAB-S09.Q05:R15-BEGIN
    base::lab::missing_algorithm("S09.Q05:R15");
    // LAB-S09.Q05:R15-END
}

[[maybe_unused]] auto has_page_read_lock(
    [[maybe_unused]] const SsiTransactionState& state,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const base::PageId page_id
) noexcept -> bool {
    // LAB-S09.Q05:R16-BEGIN
    base::lab::missing_algorithm("S09.Q05:R16");
    // LAB-S09.Q05:R16-END
}

[[maybe_unused]] auto has_exact_read_lock(
    [[maybe_unused]] const SsiTransactionState& state,
    [[maybe_unused]] const SsiReadTarget& target
) noexcept -> bool {
    // LAB-S09.Q05:R17-BEGIN
    base::lab::missing_algorithm("S09.Q05:R17");
    // LAB-S09.Q05:R17-END
}

[[maybe_unused]] void add_read_lock_holder(
    [[maybe_unused]] SsiTargetIndex& index,
    [[maybe_unused]] const SsiReadTarget& target,
    [[maybe_unused]] base::Xid xid
) {
    // LAB-S09.Q05:R18-BEGIN
    base::lab::missing_algorithm("S09.Q05:R18");
    // LAB-S09.Q05:R18-END
}

[[maybe_unused]] void rebuild_read_lock_index(
    [[maybe_unused]] const SsiStateMap& states,
    [[maybe_unused]] SsiTargetIndex& index
) {
    // LAB-S09.Q05:R19-BEGIN
    base::lab::missing_algorithm("S09.Q05:R19");
    // LAB-S09.Q05:R19-END
}

[[maybe_unused]] void promote_read_locks_to_relation(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] SsiTargetIndex& index,
    [[maybe_unused]] const base::Xid reader_xid,
    [[maybe_unused]] const base::RelationId relation_id
) {
    // LAB-S09.Q05:R20-BEGIN
    base::lab::missing_algorithm("S09.Q05:R20");
    // LAB-S09.Q05:R20-END
}

[[maybe_unused]] void promote_read_locks_to_page(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] SsiTargetIndex& index,
    [[maybe_unused]] const base::Xid reader_xid,
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const base::PageId page_id
) {
    // LAB-S09.Q05:R21-BEGIN
    base::lab::missing_algorithm("S09.Q05:R21");
    // LAB-S09.Q05:R21-END
}
}  // namespace db::transaction::detail
