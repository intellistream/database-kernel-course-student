#include <concepts>
#include <optional>
#include <type_traits>

#include "db/types/coercion.hpp"

static_assert(std::same_as<
              decltype(db::types::find_coercion_path(
                  std::declval<db::types::DataType>(),
                  std::declval<db::types::DataType>(),
                  std::declval<db::types::CoercionContext>()
              )),
              std::optional<db::types::CoercionPath>>);
static_assert(std::is_trivially_copyable_v<db::types::CoercionPath>);
static_assert(db::types::CoercionPath{}.is_relabel());
