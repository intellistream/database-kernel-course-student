#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "db/base/result.hpp"
#include "db/types/value.hpp"

namespace db::types {

auto parse_timestamp(std::string_view input) -> base::Result<TimestampValue>;
auto format_timestamp(TimestampValue value) -> std::string;
auto adjust_timestamp_precision(TimestampValue value, std::uint32_t precision)
    -> base::Result<TimestampValue>;

}  // namespace db::types
