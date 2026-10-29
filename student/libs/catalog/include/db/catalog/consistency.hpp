#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/catalog/metadata.hpp"
#include "db/types/data_type.hpp"

namespace db::catalog {

enum class IndexRebuildReason : std::uint8_t {
    kMissingRelation,
    kUnsupportedFormat,
    kSchemaMismatch,
    kCorruptRelation,
};

auto index_rebuild_reason_name(IndexRebuildReason reason) noexcept -> std::string_view;

// Borrowed expected layout passed to the composition-owned physical relation adapter. Every span
// is valid only for the duration of validate(). Implementations normally construct a
// BtreeKeyLayout and call BtreeRelation::open without retaining this request.
struct IndexRelationValidationRequest final {
    base::RelationId relation_id{base::kInvalidRelationId};
    std::span<const types::DataType> column_types{};
    IndexSchemaSignature schema_signature{kInvalidIndexSchemaSignature};
    bool unique{false};
};

class IndexRelationValidationPort {
public:
    IndexRelationValidationPort() = default;
    IndexRelationValidationPort(const IndexRelationValidationPort&) = delete;
    auto operator=(const IndexRelationValidationPort&) -> IndexRelationValidationPort& = delete;
    IndexRelationValidationPort(IndexRelationValidationPort&&) = delete;
    auto operator=(IndexRelationValidationPort&&) -> IndexRelationValidationPort& = delete;
    virtual ~IndexRelationValidationPort() = default;

    // Escaped implementation exceptions are converted to transient structured
    // errors and are never classified as rebuildable physical damage.
    virtual auto validate(IndexRelationValidationRequest request) -> base::Status = 0;
};

struct IndexRebuildDiagnostic final {
    IndexMetadata index;
    IndexRebuildReason reason{IndexRebuildReason::kMissingRelation};
    base::Error cause;

    auto operator==(const IndexRebuildDiagnostic&) const -> bool = default;
};

}  // namespace db::catalog
