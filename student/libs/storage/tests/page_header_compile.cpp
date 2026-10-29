#include <cstddef>
#include <functional>
#include <type_traits>

#include "db/storage/page.hpp"

namespace {

static_assert(sizeof(db::storage::Page) == db::storage::kPageSize);
[[maybe_unused]] constexpr db::storage::PageAddress kAddress{
    db::base::RelationId{1},
    db::base::PageId{0},
};
static_assert(kAddress.valid());
static_assert(kAddress < db::storage::PageAddress{db::base::RelationId{1}, db::base::PageId{1}});
static_assert(
    std::
        is_invocable_r_v<std::size_t, std::hash<db::storage::PageAddress>, db::storage::PageAddress>
);

}  // namespace
