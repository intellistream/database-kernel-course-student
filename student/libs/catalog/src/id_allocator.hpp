#pragma once

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/catalog/id_state.hpp"

namespace db::catalog {

// CatalogStore serializes access to this move-only allocation stream. Only CatalogIdState crosses
// the public component boundary or enters the durable format.
class CatalogIdAllocator final {
public:
    CatalogIdAllocator() = default;
    CatalogIdAllocator(const CatalogIdAllocator&) = delete;
    auto operator=(const CatalogIdAllocator&) -> CatalogIdAllocator& = delete;
    CatalogIdAllocator(CatalogIdAllocator&& other) noexcept;
    auto operator=(CatalogIdAllocator&& other) noexcept -> CatalogIdAllocator&;
    ~CatalogIdAllocator() = default;

    static auto restore(CatalogIdState state) -> base::Result<CatalogIdAllocator>;

    auto allocate_oid() -> base::Result<base::Oid>;
    auto allocate_relation_id() -> base::Result<base::RelationId>;
    auto allocate_index_id() -> base::Result<base::IndexId>;

    auto state() const noexcept -> CatalogIdState;

private:
    explicit CatalogIdAllocator(CatalogIdState state) noexcept;

    CatalogIdState state_;
};

}  // namespace db::catalog
