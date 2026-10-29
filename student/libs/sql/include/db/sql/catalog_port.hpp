#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/types/data_type.hpp"

namespace db::sql {

struct CatalogColumn final {
    std::string name;
    types::DataType data_type{types::kUnknownType};

    auto operator==(const CatalogColumn&) const -> bool = default;
};

// An owned binding descriptor, deliberately distinct from catalog's storage model. Binder ASTs
// may retain this data after the engine releases the immutable catalog generation.
struct CatalogTable final {
    base::Oid oid{base::kInvalidOid};
    std::string name;
    std::vector<CatalogColumn> columns;

    auto operator==(const CatalogTable&) const -> bool = default;
};

// Binder-owned, read-only view of one immutable catalog generation. Implementations return
// structured failures and do not throw. The interface deliberately exposes no mutation,
// persistence, statistics, physical relation, or catalog implementation API.
class CatalogReadPort {
public:
    CatalogReadPort() = default;
    CatalogReadPort(const CatalogReadPort&) = delete;
    auto operator=(const CatalogReadPort&) -> CatalogReadPort& = delete;
    CatalogReadPort(CatalogReadPort&&) = delete;
    auto operator=(CatalogReadPort&&) -> CatalogReadPort& = delete;
    virtual ~CatalogReadPort();

    virtual auto generation() const noexcept -> std::uint64_t = 0;
    virtual auto lookup_table(std::string_view name) const
        -> base::Result<std::optional<CatalogTable>> = 0;
};

}  // namespace db::sql
