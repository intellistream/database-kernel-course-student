#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/statistics.hpp"

namespace db::catalog {

class CatalogStore;

// One immutable catalog generation captured under the store mutex. Copies remain valid after DDL
// publishes a later generation and perform no I/O or locking.
class CatalogSnapshot final {
public:
    auto generation() const noexcept -> std::uint64_t;
    auto lookup_table(std::string_view name) const -> std::optional<TableMetadata>;
    auto lookup_table(base::Oid oid) const -> std::optional<TableMetadata>;
    auto list_tables() const -> std::vector<TableMetadata>;
    auto lookup_index(std::string_view name) const -> std::optional<IndexMetadata>;
    auto list_indexes() const -> std::vector<IndexMetadata>;
    auto indexes_for_table(base::Oid table_oid) const -> std::vector<IndexMetadata>;
    auto table_statistics(base::Oid table_oid) const -> std::optional<TableStatistics>;

private:
    friend class CatalogStore;

    CatalogSnapshot(
        std::uint64_t generation,
        std::vector<TableMetadata> tables,
        std::vector<IndexMetadata> indexes,
        std::vector<TableStatistics> statistics
    );

    std::uint64_t generation_;
    std::vector<TableMetadata> tables_;
    std::vector<IndexMetadata> indexes_;
    std::vector<TableStatistics> statistics_;
};

}  // namespace db::catalog
