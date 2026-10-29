#include <concepts>
#include <optional>
#include <string_view>

#include "db/types/type_registry.hpp"

static_assert(std::same_as<
              decltype(db::types::find_type_id(std::string_view{})),
              std::optional<db::base::TypeId>>);
static_assert(std::same_as<
              decltype(db::types::type_name(db::types::kBoolTypeId)),
              std::optional<std::string_view>>);
