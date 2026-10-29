#include "db/catalog/metadata.hpp"

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/base/lab_missing.hpp"
#include "db/catalog/index_schema.hpp"
#include "db/catalog/schema.hpp"

#include "metadata_validation.hpp"

namespace db::catalog {

[[maybe_unused]] TableMetadata::TableMetadata([[maybe_unused]] TableMetadataDefinition definition)
    : oid_{definition.oid},
      relation_id_{definition.relation_id},
      name_{std::move(definition.name)},
      schema_{std::move(definition.schema)},
      fillfactor_{definition.fillfactor} {
    // LAB-S02.Q10:R13-BEGIN
    base::lab::missing_algorithm("S02.Q10:R13");
    // LAB-S02.Q10:R13-END
}

[[maybe_unused]] auto TableMetadata::create([[maybe_unused]] TableMetadataDefinition definition)
    -> base::Result<TableMetadata> {
    // LAB-S02.Q10:R14-BEGIN
    base::lab::missing_algorithm("S02.Q10:R14");
    // LAB-S02.Q10:R14-END
}

auto TableMetadata::oid() const noexcept -> base::Oid {
    return oid_;
}

auto TableMetadata::relation_id() const noexcept -> base::RelationId {
    return relation_id_;
}

auto TableMetadata::name() const noexcept -> std::string_view {
    return name_;
}

auto TableMetadata::schema() const noexcept -> const Schema& {
    return schema_;
}

auto TableMetadata::fillfactor() const noexcept -> std::uint8_t {
    return fillfactor_;
}

[[maybe_unused]] IndexMetadata::IndexMetadata([[maybe_unused]] IndexMetadataDefinition definition)
    : oid_{definition.oid},
      index_id_{definition.index_id},
      relation_id_{definition.relation_id},
      table_oid_{definition.table_oid},
      name_{std::move(definition.name)},
      column_names_{std::move(definition.column_names)},
      schema_signature_{definition.schema_signature},
      unique_{definition.unique} {
    // LAB-S02.Q10:R15-BEGIN
    base::lab::missing_algorithm("S02.Q10:R15");
    // LAB-S02.Q10:R15-END
}

[[maybe_unused]] auto IndexMetadata::create([[maybe_unused]] IndexMetadataDefinition definition)
    -> base::Result<IndexMetadata> {
    // LAB-S02.Q10:R16-BEGIN
    base::lab::missing_algorithm("S02.Q10:R16");
    // LAB-S02.Q10:R16-END
}

auto IndexMetadata::oid() const noexcept -> base::Oid {
    return oid_;
}

auto IndexMetadata::index_id() const noexcept -> base::IndexId {
    return index_id_;
}

auto IndexMetadata::relation_id() const noexcept -> base::RelationId {
    return relation_id_;
}

auto IndexMetadata::table_oid() const noexcept -> base::Oid {
    return table_oid_;
}

auto IndexMetadata::name() const noexcept -> std::string_view {
    return name_;
}

auto IndexMetadata::column_names() const noexcept -> std::span<const std::string> {
    return column_names_;
}

[[maybe_unused]] auto IndexMetadata::schema_signature() const noexcept -> IndexSchemaSignature {
    // LAB-S02.Q10:R17-BEGIN
    base::lab::missing_algorithm("S02.Q10:R17");
    // LAB-S02.Q10:R17-END
}

auto IndexMetadata::unique() const noexcept -> bool {
    return unique_;
}

}  // namespace db::catalog
