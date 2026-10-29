#include <type_traits>

#include "detail/file_layout.hpp"

namespace {

[[maybe_unused]] constexpr auto kRelationFileLayoutSize =
    sizeof(db::storage::detail::RelationFileLayout);
static_assert(!std::is_copy_constructible_v<db::storage::detail::RelationFileLayout>);
static_assert(std::is_nothrow_move_constructible_v<db::storage::detail::RelationFileLayout>);
static_assert(std::is_nothrow_move_assignable_v<db::storage::detail::RelationFileLayout>);

}  // namespace
