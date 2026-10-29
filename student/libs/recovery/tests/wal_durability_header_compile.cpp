#include <type_traits>

#include "db/recovery/wal_durability.hpp"

namespace {

static_assert(std::is_final_v<db::recovery::WalPageDurabilityAdapter>);
static_assert(
    std::is_base_of_v<db::storage::PageDurabilityPort, db::recovery::WalPageDurabilityAdapter>
);

}  // namespace
