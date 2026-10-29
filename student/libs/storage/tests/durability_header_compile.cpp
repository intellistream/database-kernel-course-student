#include <type_traits>

#include "db/storage/durability.hpp"

namespace {

static_assert(std::is_abstract_v<db::storage::PageDurabilityPort>);
static_assert(!std::is_copy_constructible_v<db::storage::PageDurabilityPort>);

}  // namespace
