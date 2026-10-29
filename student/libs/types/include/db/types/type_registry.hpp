#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "db/base/result.hpp"
#include "db/types/data_type.hpp"

namespace db::types {

// The descriptor table and its storage policy are private implementation
// details. Components resolve stable identities and names through these owned
// value queries rather than retaining pointers into registry storage.
auto find_type_id(std::string_view name) noexcept -> std::optional<TypeId>;
auto type_name(TypeId id) noexcept -> std::optional<std::string_view>;
auto is_known_type(TypeId id) noexcept -> bool;
auto validate_data_type(DataType data_type) -> base::Status;
auto storage_width(DataType data_type) -> base::Result<std::size_t>;
auto is_storable(DataType data_type) -> bool;

}  // namespace db::types
