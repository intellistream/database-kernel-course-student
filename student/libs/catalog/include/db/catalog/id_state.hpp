#pragma once

#include <cstdint>

namespace db::catalog {

// Durable identifier frontiers. A frontier can be one past UINT32_MAX to represent a permanently
// exhausted domain without wrapping to the invalid identifier zero.
struct CatalogIdState final {
    std::uint64_t next_oid{1};
    std::uint64_t next_relation_id{1};
    std::uint64_t next_index_id{1};

    auto operator==(const CatalogIdState&) const -> bool = default;
};

}  // namespace db::catalog
