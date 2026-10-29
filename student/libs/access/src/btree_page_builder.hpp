#pragma once

#include <span>

#include "db/access/btree_page.hpp"

namespace db::access {

// Borrowed canonical data consumed only while the caller holds the page's
// exclusive latch. The bulk builder orders keys before creating these views;
// this helper writes their physical tuples once.
struct BtreeBulkLeafEntry final {
    base::Bytes key;
    base::Rid heap_rid{base::kInvalidRid};
};

struct BtreeBulkInternalEntry final {
    base::Bytes separator;
    base::PageId child_page_id{base::kInvalidPageId};
};

class BtreeBulkPageBuilder final {
public:
    static void
    append_leaf_entries(BtreePage& page, std::span<const BtreeBulkLeafEntry> entries) noexcept;
    static void append_internal_entries(
        BtreePage& page,
        base::PageId first_child,
        std::span<const BtreeBulkInternalEntry> entries
    ) noexcept;
};

}  // namespace db::access
