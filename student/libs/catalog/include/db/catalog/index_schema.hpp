#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "db/base/result.hpp"
#include "db/catalog/schema.hpp"
#include "db/types/data_type.hpp"

namespace db::catalog {

// Stable BKL1 signature of an index's ordered key DataTypes. It is persisted by catalog v1 and
// must equal the physical B+Tree metapage signature, but it does not duplicate page/root state.
class IndexSchemaSignature final {
public:
    constexpr explicit IndexSchemaSignature(const std::uint64_t value) noexcept
        : value_{value} {}

    constexpr auto value() const noexcept -> std::uint64_t {
        return value_;
    }

    constexpr auto operator==(const IndexSchemaSignature&) const noexcept -> bool = default;
    constexpr auto operator<=>(const IndexSchemaSignature&) const noexcept = default;

private:
    std::uint64_t value_;
};

inline constexpr IndexSchemaSignature kInvalidIndexSchemaSignature{0U};

struct ResolvedIndexSchema final {
    std::vector<std::size_t> column_positions;
    std::vector<types::DataType> column_types;
    IndexSchemaSignature signature;

    auto operator==(const ResolvedIndexSchema&) const -> bool = default;
};

// Resolves every key name against one immutable table schema in order and computes the exact BKL1
// signature consumed by access. Repeated key names retain their requested positions.
auto resolve_index_schema(const Schema& schema, std::span<const std::string> column_names)
    -> base::Result<ResolvedIndexSchema>;

// Exposed for cross-component format checks and physical relation adapters. Callers must supply a
// nonempty sequence no larger than the catalog u16 key-count limit.
auto index_schema_signature(std::span<const types::DataType> column_types)
    -> base::Result<IndexSchemaSignature>;

}  // namespace db::catalog
