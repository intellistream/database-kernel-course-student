#include <concepts>
#include <cstdint>
#include <type_traits>

#include "db/base/identifier.hpp"

static_assert(std::same_as<db::base::PageId::RepresentationType, std::uint32_t>);
static_assert(std::same_as<db::base::Lsn::RepresentationType, std::uint64_t>);
static_assert(!std::is_convertible_v<std::uint32_t, db::base::PageId>);
