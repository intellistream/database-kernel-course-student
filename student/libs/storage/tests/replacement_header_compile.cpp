#include <type_traits>

#include "db/storage/replacement.hpp"

namespace {

template <typename Type>
concept ExposesReplacementImplementation = requires { typename Type::Impl; };

static_assert(std::has_virtual_destructor_v<db::storage::ReplacementPolicy>);
static_assert(!std::is_copy_constructible_v<db::storage::LruReplacer>);
static_assert(!std::is_copy_constructible_v<db::storage::ClockReplacer>);
static_assert(!ExposesReplacementImplementation<db::storage::LruReplacer>);
static_assert(!ExposesReplacementImplementation<db::storage::ClockReplacer>);

}  // namespace
