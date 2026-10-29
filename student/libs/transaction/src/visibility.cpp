#include "db/transaction/visibility.hpp"

#include "db/base/lab_missing.hpp"
#include "db/transaction/xid.hpp"

namespace db::transaction {
namespace {

[[maybe_unused]] auto decision(TupleModificationResult result) noexcept
    -> TupleModificationDecision {
    return TupleModificationDecision{.result = result, .blocking_xid = base::kInvalidXid};
}

[[maybe_unused]] auto creator_visible_in_snapshot(
    [[maybe_unused]] const TupleVersionState& tuple,
    [[maybe_unused]] const base::Xid own_xid,
    [[maybe_unused]] const Snapshot& snapshot,
    [[maybe_unused]] const CommitLog& commit_log
) noexcept -> bool {
    // LAB-S09.Q03:R01-BEGIN
    base::lab::missing_algorithm("S09.Q03:R01");
    // LAB-S09.Q03:R01-END
}

}  // namespace

[[maybe_unused]] auto xid_in_mvcc_snapshot(
    [[maybe_unused]] base::Xid xid,
    [[maybe_unused]] const Snapshot& snapshot
) noexcept -> bool {
    // LAB-S09.Q03:R02-BEGIN
    base::lab::missing_algorithm("S09.Q03:R02");
    // LAB-S09.Q03:R02-END
}

[[maybe_unused]] auto tuple_visible_in_snapshot(
    [[maybe_unused]] const TupleVersionState& tuple,
    [[maybe_unused]] base::Xid own_xid,
    [[maybe_unused]] const Snapshot& snapshot,
    [[maybe_unused]] const CommitLog& commit_log
) noexcept -> bool {
    // LAB-S09.Q03:R03-BEGIN
    base::lab::missing_algorithm("S09.Q03:R03");
    // LAB-S09.Q03:R03-END
}

[[maybe_unused]] auto tuple_satisfies_update(
    [[maybe_unused]] const TupleVersionState& tuple,
    [[maybe_unused]] base::Xid own_xid,
    [[maybe_unused]] const Snapshot& snapshot,
    [[maybe_unused]] const CommitLog& commit_log
) noexcept -> TupleModificationDecision {
    // LAB-S09.Q03:R04-BEGIN
    base::lab::missing_algorithm("S09.Q03:R04");
    // LAB-S09.Q03:R04-END
}

}  // namespace db::transaction
