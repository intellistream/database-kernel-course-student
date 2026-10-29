#pragma once

#include <optional>

#include "db/types/data_type.hpp"
#include "db/types/operator.hpp"

namespace db::types {

// Returns only the resolved result identity. The operation and operand types
// are already inputs, so repeating them in an open descriptor would allow
// callers to manufacture contradictory signatures.
auto resolve_binary_operator(BinaryOperator operation, TypeId left, TypeId right) noexcept
    -> std::optional<TypeId>;

}  // namespace db::types
