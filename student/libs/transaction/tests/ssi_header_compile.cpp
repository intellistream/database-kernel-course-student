#include <concepts>
#include <functional>

#include "db/transaction/ssi.hpp"

template <typename Type>
concept ExposesSsiImplementation = requires { typename Type::Inner; };

static_assert(std::regular_invocable<
              std::hash<db::transaction::SsiReadTarget>,
              const db::transaction::SsiReadTarget&>);
static_assert(!ExposesSsiImplementation<db::transaction::SerializableConflictManager>);

namespace {

[[maybe_unused]] void ssi_header_is_self_contained() {
    const auto target = db::transaction::SsiReadTarget::for_relation(db::base::RelationId{1});
    (void)target;
}

}  // namespace
