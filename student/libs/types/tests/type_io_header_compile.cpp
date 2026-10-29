#include <concepts>
#include <string_view>

#include "db/types/type_io.hpp"

static_assert(std::same_as<
              decltype(db::types::input_value(
                  std::declval<db::types::DataType>(),
                  std::declval<std::string_view>()
              )),
              db::base::Result<db::types::Value>>);
