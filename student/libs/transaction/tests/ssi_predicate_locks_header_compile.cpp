#include "ssi_predicate_locks.hpp"

namespace {

[[maybe_unused]] void compile_ssi_predicate_locks_header() {
    db::transaction::detail::SsiStateMap states;
    db::transaction::detail::SsiPredicateIndex predicate_index;
    const auto candidates = db::transaction::detail::collect_ssi_predicate_candidates(
        states,
        predicate_index,
        db::base::Xid{1},
        db::base::RelationId{1}
    );
    (void)candidates;
}

}  // namespace
