#pragma once

#include "db/access/btree_wal.hpp"
#include "db/catalog/consistency.hpp"
#include "db/storage/buffer_pool.hpp"

namespace db::recovery {

// Composition adapter that checks the catalog/access BKL1 implementation and acquires the physical
// B+Tree relation. Resident metapage bytes follow the access trusted-page contract and are not
// audited here. Catalog owns the port; this layer may depend on catalog and access/storage.
class BtreeCatalogValidationAdapter final : public catalog::IndexRelationValidationPort {
public:
    BtreeCatalogValidationAdapter(
        storage::BufferPool& pool,
        access::BtreeMutationLogPort& mutation_log
    ) noexcept;

    auto validate(catalog::IndexRelationValidationRequest request) -> base::Status override;

private:
    storage::BufferPool* pool_;
    access::BtreeMutationLogPort* mutation_log_;
};

}  // namespace db::recovery
