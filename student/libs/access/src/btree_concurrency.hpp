#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "db/access/btree_relation.hpp"
#include "db/base/lab_missing.hpp"

namespace db::access {

// Relation-local, process-local B-tree state. The cached root is a PG-style
// fast-root hint. Root splits leave the old root as the leftmost B-link page at
// its original level, so consumers can pin it and descend without a metapage
// round trip. Keeping the state behind a shared handle lets scans retain the
// hint after their originating BtreeRelation facade is moved.
class BtreeRelation::State final {
public:
    [[maybe_unused]] auto root_hint() const -> std::optional<BtreeRoot> {
        // LAB-S03.Q03:R01-BEGIN
        base::lab::missing_algorithm("S03.Q03:R01");
        // LAB-S03.Q03:R01-END
    }

    [[maybe_unused]] void set_root_hint([[maybe_unused]] const std::optional<BtreeRoot> root) {
        // LAB-S03.Q03:R02-BEGIN
        base::lab::missing_algorithm("S03.Q03:R02");
        // LAB-S03.Q03:R02-END
    }

    [[maybe_unused]] void clear_root_hint_if([[maybe_unused]] const BtreeRoot root) {
        // LAB-S03.Q03:R03-BEGIN
        base::lab::missing_algorithm("S03.Q03:R03");
        // LAB-S03.Q03:R03-END
    }

private:
    mutable std::mutex mutex_;
    std::optional<BtreeRoot> root_hint_;
};

}  // namespace db::access
