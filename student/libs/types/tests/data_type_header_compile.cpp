#include <compare>
#include <type_traits>

#include "db/types/data_type.hpp"

static_assert(std::three_way_comparable<db::types::TypeModifier>);
static_assert(std::three_way_comparable<db::types::DataType>);
static_assert(std::is_trivially_copyable_v<db::types::DataType>);
