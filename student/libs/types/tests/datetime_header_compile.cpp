#include <concepts>
#include <string_view>

#include "db/types/datetime.hpp"

static_assert(std::same_as<
              decltype(db::types::parse_timestamp(std::declval<std::string_view>())),
              db::base::Result<db::types::TimestampValue>>);
