#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/catalog/consistency.hpp"
#include "db/catalog/format.hpp"
#include "db/catalog/id_state.hpp"
#include "db/catalog/metadata.hpp"
#include "db/catalog/snapshot.hpp"
#include "db/catalog/statistics.hpp"

namespace db::catalog {

class CatalogPublicationPort {
public:
    CatalogPublicationPort() = default;
    CatalogPublicationPort(const CatalogPublicationPort&) = delete;
    auto operator=(const CatalogPublicationPort&) -> CatalogPublicationPort& = delete;
    CatalogPublicationPort(CatalogPublicationPort&&) = delete;
    auto operator=(CatalogPublicationPort&&) -> CatalogPublicationPort& = delete;
    virtual ~CatalogPublicationPort() = default;

    // Synchronous publication boundary. Escaped implementation exceptions are
    // converted to structured catalog failures and reconciled exactly like a
    // returned failure; no exception crosses a CatalogStore entry point.
    virtual auto
    publish(const std::filesystem::path& path, base::Bytes image, std::uint32_t permissions)
        -> base::Status = 0;
};

struct CatalogStoreOptions final {
    CatalogDecodeOptions decode;
    std::uint32_t permissions{0640U};
};

struct CreateTableRequest final {
    std::string name;
    Schema schema;
    std::uint8_t fillfactor{kDefaultHeapFillfactor};
    bool if_not_exists{false};
};

struct CreateIndexRequest final {
    std::string name;
    std::string table_name;
    std::vector<std::string> column_names;
    bool unique{false};
    bool if_not_exists{false};
};

struct DropTableResult final {
    TableMetadata table;
    std::vector<IndexMetadata> indexes;

    auto operator==(const DropTableResult&) const -> bool = default;
};

// A process-local catalog facade. Mutations are copy-on-publish: candidate state becomes visible
// only after complete-image publication, or after failure reconciliation observes that rename did
// publish it. Physical heap/B+Tree creation and removal remain composition-layer responsibilities.
class CatalogStore final {
public:
    static auto create(
        std::filesystem::path path,
        CatalogStoreOptions options,
        std::unique_ptr<CatalogPublicationPort> publication = {}
    ) -> base::Result<CatalogStore>;
    static auto open(
        std::filesystem::path path,
        CatalogStoreOptions options,
        std::unique_ptr<CatalogPublicationPort> publication = {}
    ) -> base::Result<CatalogStore>;

    CatalogStore(const CatalogStore&) = delete;
    auto operator=(const CatalogStore&) -> CatalogStore& = delete;
    CatalogStore(CatalogStore&& other) noexcept;
    auto operator=(CatalogStore&& other) noexcept -> CatalogStore&;
    ~CatalogStore();

    auto valid() const noexcept -> bool;
    auto generation() const -> base::Result<std::uint64_t>;
    auto id_state() const -> base::Result<CatalogIdState>;
    auto snapshot() const -> base::Result<CatalogSnapshot>;
    auto lookup_table(std::string_view name) const -> base::Result<std::optional<TableMetadata>>;
    auto lookup_table(base::Oid oid) const -> base::Result<std::optional<TableMetadata>>;
    auto list_tables() const -> base::Result<std::vector<TableMetadata>>;
    auto lookup_index(std::string_view name) const -> base::Result<std::optional<IndexMetadata>>;
    auto list_indexes() const -> base::Result<std::vector<IndexMetadata>>;
    auto indexes_for_table(base::Oid table_oid) const -> base::Result<std::vector<IndexMetadata>>;
    auto table_statistics(base::Oid table_oid) const
        -> base::Result<std::optional<TableStatistics>>;
    auto list_statistics() const -> base::Result<std::vector<TableStatistics>>;

    // Raises only the OID allocation floor when startup recovery has observed
    // a durable checkpoint farther ahead than this FCAT generation. Relation
    // and index IDs are distinct domains and deliberately remain unchanged.
    // Returns true exactly when a new complete FCAT generation was published.
    auto ensure_next_oid_at_least(std::uint32_t next_oid) -> base::Result<bool>;

    auto audit_indexes(IndexRelationValidationPort& validation) const
        -> base::Result<std::vector<IndexRebuildDiagnostic>>;
    auto require_index_consistency(IndexRelationValidationPort& validation) const -> base::Status;

    auto create_table(CreateTableRequest request) -> base::Result<std::optional<TableMetadata>>;
    auto create_index(CreateIndexRequest request) -> base::Result<std::optional<IndexMetadata>>;
    auto drop_table(std::string_view name, bool if_exists)
        -> base::Result<std::optional<DropTableResult>>;
    auto drop_index(std::string_view name, bool if_exists)
        -> base::Result<std::optional<IndexMetadata>>;

    // A complete ANALYZE batch is validated first and published as one generation. Empty batches
    // and invalidations of already-absent statistics are publication-free no-ops.
    auto install_statistics(std::vector<TableStatistics> statistics) -> base::Status;
    auto invalidate_statistics(base::Oid table_oid) -> base::Result<bool>;

private:
    struct Impl;

    explicit CatalogStore(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

}  // namespace db::catalog
