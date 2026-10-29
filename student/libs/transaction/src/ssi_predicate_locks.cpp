#include "ssi_predicate_locks.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

#include "ssi_conflict_graph.hpp"

namespace db::transaction::detail {
namespace {

[[maybe_unused]] auto predicate_lock_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kTransactionState, std::move(message)}.with_context(
        "transaction/ssi",
        "check sequential predicate write conflict"
    );
}

[[maybe_unused]] auto contains_xid(const std::vector<base::Xid>& values, base::Xid xid) noexcept
    -> bool {
    return std::ranges::find(values, xid) != values.end();
}

}  // namespace

[[maybe_unused]] auto has_all_ssi_predicate_lock(
    [[maybe_unused]] const SsiTransactionState& state,
    [[maybe_unused]] const base::RelationId relation_id
) noexcept -> bool {
    // LAB-S09.Q05:R11-BEGIN
    base::lab::missing_algorithm("S09.Q05:R11");
    // LAB-S09.Q05:R11-END
}

[[maybe_unused]] auto collect_ssi_predicate_candidates(
    [[maybe_unused]] SsiStateMap& states,
    [[maybe_unused]] const SsiPredicateIndex& predicate_index,
    [[maybe_unused]] const base::Xid writer_xid,
    [[maybe_unused]] const base::RelationId relation_id
) -> base::Result<std::vector<PredicateCandidate>> {
    // LAB-S09.Q05:R12-BEGIN
    base::lab::missing_algorithm("S09.Q05:R12");
    // LAB-S09.Q05:R12-END
}

[[maybe_unused]] auto revalidate_ssi_predicate_readers(
    [[maybe_unused]] const SsiStateMap& states,
    [[maybe_unused]] const base::Xid writer_xid,
    [[maybe_unused]] const std::vector<PredicateCandidate>& matches
) -> std::vector<base::Xid> {
    // LAB-S09.Q05:R13-BEGIN
    base::lab::missing_algorithm("S09.Q05:R13");
    // LAB-S09.Q05:R13-END
}
}  // namespace db::transaction::detail
