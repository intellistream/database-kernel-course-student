#include "db/transaction/visibility.hpp"

namespace {

[[maybe_unused]] void visibility_header_is_self_contained() {
    const db::transaction::TupleVersionState tuple;
    const db::transaction::TupleModificationDecision decision;
    (void)tuple;
    (void)decision;
}

}  // namespace
