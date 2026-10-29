#pragma once

#include <string_view>

#include "db/base/result.hpp"
#include "db/types/data_type.hpp"

namespace db::types {

auto validate_canonical_character(
    std::string_view text,
    DataType data_type,
    std::string_view format
) -> base::Result<std::string_view>;

}  // namespace db::types
