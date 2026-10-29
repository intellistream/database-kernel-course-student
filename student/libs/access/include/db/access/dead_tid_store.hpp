#pragma once

#include <cstddef>
#include <span>
#include <unordered_set>
#include <vector>

#include "db/base/identifier.hpp"

namespace db::access {

// Deterministically ordered equivalent of PostgreSQL's lazy-VACUUM TID store.
// Heap-dead entries remain stable until every index has consumed this set.
class DeadTidStore final {
public:
    auto insert(base::Rid rid) -> bool;
    auto contains(base::Rid rid) const -> bool;
    auto empty() const noexcept -> bool;
    auto size() const noexcept -> std::size_t;
    auto tids() const -> std::span<const base::Rid>;

private:
    std::unordered_set<base::Rid> membership_;
    mutable std::vector<base::Rid> ordered_tids_;
    mutable bool order_dirty_{false};
};

}  // namespace db::access
