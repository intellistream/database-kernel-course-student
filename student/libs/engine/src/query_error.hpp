#pragma once

#include <string>

#include "db/base/error.hpp"

namespace db::engine::detail {

auto query_error(base::ErrorCode code, std::string message, std::string operation) -> base::Error;

}  // namespace db::engine::detail
