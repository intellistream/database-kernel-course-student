#include "format_validation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/statistics.hpp"
#include "db/types/codec.hpp"

#include "id_allocator.hpp"

namespace db::catalog {
namespace {

constexpr std::uint64_t kExhaustedFrontier =
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U;

auto valid_frontier_for_maximum(const std::uint64_t frontier, const std::uint32_t maximum) noexcept
    -> bool {
    return frontier >= 1U && frontier <= kExhaustedFrontier
           && frontier > static_cast<std::uint64_t>(maximum);
}

auto add_encoded_size(std::size_t& total, const std::size_t amount) -> base::Status {
    if (amount > kDefaultMaximumCatalogImageSize - total) {
        return base::fail(
            base::Error{
                base::ErrorCode::kResourceExhausted,
                "catalog image exceeds the default format safety limit",
            }
        );
    }
    total += amount;
    return {};
}

auto add_table_encoded_sizes(std::size_t& total, const std::span<const TableMetadata> tables)
    -> base::Status {
    for (const auto& table : tables) {
        if (auto status = add_encoded_size(total, kTableRecordFixedSize + table.name().size());
            !status) {
            return status;
        }
        for (const auto& column : table.schema().columns()) {
            if (auto status = add_encoded_size(total, kColumnRecordFixedSize + column.name.size());
                !status) {
                return status;
            }
        }
    }
    return {};
}

auto add_index_encoded_sizes(std::size_t& total, const std::span<const IndexMetadata> indexes)
    -> base::Status {
    for (const auto& index : indexes) {
        if (auto status = add_encoded_size(total, kIndexRecordFixedSize + index.name().size());
            !status) {
            return status;
        }
        for (const auto& column_name : index.column_names()) {
            if (auto status = add_encoded_size(total, sizeof(std::uint16_t) + column_name.size());
                !status) {
                return status;
            }
        }
    }
    return {};
}

auto add_statistics_encoded_sizes(
    std::size_t& total,
    const std::span<const TableStatistics> statistics_values
) -> base::Status {
    for (const auto& statistics : statistics_values) {
        if (auto status = add_encoded_size(total, kStatisticsRecordFixedSize); !status) {
            return status;
        }
        for (const auto& column : statistics.columns) {
            auto minimum = encode_statistics_bound(column.minimum, column.data_type);
            auto maximum = encode_statistics_bound(column.maximum, column.data_type);
            if (!minimum || !maximum) {
                return base::fail(
                    !minimum ? std::move(minimum).error() : std::move(maximum).error()
                );
            }
            if (auto status = add_encoded_size(
                    total,
                    kColumnStatisticsRecordFixedSize + minimum->size() + maximum->size()
                );
                !status) {
                return status;
            }
        }
    }
    return {};
}

auto validate_encoded_size(const CatalogImage& image) -> base::Status {
    std::size_t total = kCatalogFormatHeaderSize;
    if (auto status = add_table_encoded_sizes(total, image.tables); !status) {
        return status;
    }
    if (auto status = add_index_encoded_sizes(total, image.indexes); !status) {
        return status;
    }
    return add_statistics_encoded_sizes(total, image.statistics);
}

auto validate_index_schema_relationships(
    const CatalogImage& image,
    const std::unordered_map<base::Oid, const TableMetadata*>& tables_by_oid
) -> base::Status {
    for (const auto& index : image.indexes) {
        const auto owner = tables_by_oid.find(index.table_oid());
        if (owner == tables_by_oid.end()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "index '" + std::string{index.name()} + "' refers to a missing owner table",
                }
            );
        }
        auto resolved = resolve_index_schema(owner->second->schema(), index.column_names());
        if (!resolved) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "index '" + std::string{index.name()}
                        + "' has an invalid key schema: " + std::string{resolved.error().message()},
                }
            );
        }
        if (resolved->signature != index.schema_signature()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "index '" + std::string{index.name()} + "' schema signature is stale",
                }
            );
        }
    }
    return {};
}

auto validate_statistics_relationships(
    const CatalogImage& image,
    const std::unordered_map<base::Oid, const TableMetadata*>& tables_by_oid
) -> base::Status {
    std::unordered_set<base::Oid> statistics_tables;
    statistics_tables.reserve(image.statistics.size());
    for (const auto& statistics : image.statistics) {
        if (!statistics_tables.insert(statistics.table_oid).second) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kAlreadyExists,
                    "catalog contains duplicate table statistics",
                }
            );
        }
        const auto owner = tables_by_oid.find(statistics.table_oid);
        if (owner == tables_by_oid.end()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "table statistics refer to a missing owner table",
                }
            );
        }
        if (auto status = validate_table_statistics(statistics, owner->second->schema()); !status) {
            return status;
        }
    }
    return {};
}

}  // namespace

auto encode_statistics_bound(
    const std::optional<types::Value>& bound,
    const types::DataType data_type
) -> base::Result<std::vector<std::byte>> {
    if (!bound) {
        return std::vector<std::byte>{};
    }
    return types::heap_encode(*bound, data_type);
}

// One model-level pass owns cross-table identity, schema, index, and statistics consistency.
// NOLINTNEXTLINE(readability-function-size)
auto validate_catalog_image_model(const CatalogImage& image) -> base::Status {
    if (image.generation == 0U) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "catalog generation must be nonzero",
            }
        );
    }
    if (!CatalogIdAllocator::restore(image.id_state)) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "catalog identifier frontiers are outside the representable range",
            }
        );
    }
    if (image.tables.size() > kMaximumCatalogObjects
        || image.indexes.size() > kMaximumCatalogObjects
        || image.statistics.size() > kMaximumCatalogObjects) {
        return base::fail(
            base::Error{
                base::ErrorCode::kResourceExhausted,
                "catalog object count exceeds the format safety limit",
            }
        );
    }

    std::unordered_set<base::Oid> oids;
    std::unordered_set<base::RelationId> relations;
    std::unordered_set<base::IndexId> index_ids;
    std::unordered_set<std::string_view> table_names;
    std::unordered_set<std::string_view> index_names;
    std::unordered_map<base::Oid, const TableMetadata*> tables_by_oid;
    oids.reserve(image.tables.size() + image.indexes.size());
    relations.reserve(image.tables.size() + image.indexes.size());
    table_names.reserve(image.tables.size());
    index_names.reserve(image.indexes.size());
    index_ids.reserve(image.indexes.size());
    tables_by_oid.reserve(image.tables.size());

    std::uint32_t maximum_oid = 0U;
    std::uint32_t maximum_relation = 0U;
    std::uint32_t maximum_index = 0U;
    for (const auto& table : image.tables) {
        auto validated = TableMetadata::create(
            TableMetadataDefinition{
                .oid = table.oid(),
                .relation_id = table.relation_id(),
                .name = std::string{table.name()},
                .schema = table.schema(),
                .fillfactor = table.fillfactor(),
            }
        );
        if (!validated) {
            return base::fail(std::move(validated).error());
        }
        if (table.schema().size() > kMaximumColumnsPerTable) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kResourceExhausted,
                    "table column count exceeds the catalog format limit",
                }
            );
        }
        if (!oids.insert(table.oid()).second || !relations.insert(table.relation_id()).second
            || !table_names.insert(table.name()).second) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kAlreadyExists,
                    "catalog contains a duplicate table identity or name",
                }
            );
        }
        maximum_oid = std::max(maximum_oid, table.oid().value());
        maximum_relation = std::max(maximum_relation, table.relation_id().value());
        tables_by_oid.emplace(table.oid(), &table);
    }
    for (const auto& index : image.indexes) {
        auto validated = IndexMetadata::create(
            IndexMetadataDefinition{
                .oid = index.oid(),
                .index_id = index.index_id(),
                .relation_id = index.relation_id(),
                .table_oid = index.table_oid(),
                .name = std::string{index.name()},
                .column_names =
                    std::vector<std::string>{
                        index.column_names().begin(),
                        index.column_names().end()
                    },
                .schema_signature = index.schema_signature(),
                .unique = index.unique(),
            }
        );
        if (!validated) {
            return base::fail(std::move(validated).error());
        }
        if (!oids.insert(index.oid()).second || !relations.insert(index.relation_id()).second
            || !index_ids.insert(index.index_id()).second
            || !index_names.insert(index.name()).second) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kAlreadyExists,
                    "catalog contains a duplicate index identity or name",
                }
            );
        }
        maximum_oid = std::max(maximum_oid, index.oid().value());
        maximum_relation = std::max(maximum_relation, index.relation_id().value());
        maximum_index = std::max(maximum_index, index.index_id().value());
    }
    if (auto status = validate_index_schema_relationships(image, tables_by_oid); !status) {
        return status;
    }
    if (auto status = validate_statistics_relationships(image, tables_by_oid); !status) {
        return status;
    }
    if (!valid_frontier_for_maximum(image.id_state.next_oid, maximum_oid)
        || !valid_frontier_for_maximum(image.id_state.next_relation_id, maximum_relation)
        || !valid_frontier_for_maximum(image.id_state.next_index_id, maximum_index)) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "catalog identifier frontier does not cover every allocated identity",
            }
        );
    }
    return validate_encoded_size(image);
}

}  // namespace db::catalog
