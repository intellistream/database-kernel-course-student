#pragma once

#include <string_view>

#include "db/base/result.hpp"

namespace db::catalog {

auto validate_catalog_name(std::string_view name, std::string_view subject) -> base::Status;

}  // namespace db::catalog
