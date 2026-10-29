#include <type_traits>

#include "consistency_internal.hpp"

static_assert(std::is_abstract_v<db::catalog::IndexRelationValidationPort>);
