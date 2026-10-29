#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/catalog/index_schema.hpp"
#include "db/catalog/schema.hpp"

namespace db::catalog {

inline constexpr std::uint8_t kDefaultHeapFillfactor = 80;
inline constexpr std::uint8_t kMinimumHeapFillfactor = 10;

struct TableMetadataDefinition final {
    base::Oid oid{base::kInvalidOid};
    base::RelationId relation_id{base::kInvalidRelationId};
    std::string name;
    Schema schema;
    std::uint8_t fillfactor{kDefaultHeapFillfactor};
};

class TableMetadata final {
public:
    static auto create(TableMetadataDefinition definition) -> base::Result<TableMetadata>;

    auto oid() const noexcept -> base::Oid;
    auto relation_id() const noexcept -> base::RelationId;
    auto name() const noexcept -> std::string_view;
    auto schema() const noexcept -> const Schema&;
    auto fillfactor() const noexcept -> std::uint8_t;

    auto operator==(const TableMetadata&) const -> bool = default;

private:
    explicit TableMetadata(TableMetadataDefinition definition);

    base::Oid oid_;
    base::RelationId relation_id_;
    std::string name_;
    Schema schema_;
    std::uint8_t fillfactor_;
};

struct IndexMetadataDefinition final {
    base::Oid oid{base::kInvalidOid};
    base::IndexId index_id{base::kInvalidIndexId};
    base::RelationId relation_id{base::kInvalidRelationId};
    base::Oid table_oid{base::kInvalidOid};
    std::string name;
    std::vector<std::string> column_names;
    IndexSchemaSignature schema_signature{kInvalidIndexSchemaSignature};
    bool unique{false};
};

class IndexMetadata final {
public:
    static auto create(IndexMetadataDefinition definition) -> base::Result<IndexMetadata>;

    auto oid() const noexcept -> base::Oid;
    auto index_id() const noexcept -> base::IndexId;
    auto relation_id() const noexcept -> base::RelationId;
    auto table_oid() const noexcept -> base::Oid;
    auto name() const noexcept -> std::string_view;
    auto column_names() const noexcept -> std::span<const std::string>;
    auto schema_signature() const noexcept -> IndexSchemaSignature;
    auto unique() const noexcept -> bool;

    auto operator==(const IndexMetadata&) const -> bool = default;

private:
    explicit IndexMetadata(IndexMetadataDefinition definition);

    base::Oid oid_;
    base::IndexId index_id_;
    base::RelationId relation_id_;
    base::Oid table_oid_;
    std::string name_;
    std::vector<std::string> column_names_;
    IndexSchemaSignature schema_signature_;
    bool unique_;
};

}  // namespace db::catalog
