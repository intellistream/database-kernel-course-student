#include <concepts>
#include <optional>

#include "db/types/operator_registry.hpp"

static_assert(std::same_as<
              decltype(db::types::resolve_binary_operator(
                  std::declval<db::types::BinaryOperator>(),
                  std::declval<db::types::TypeId>(),
                  std::declval<db::types::TypeId>()
              )),
              std::optional<db::types::TypeId>>);
