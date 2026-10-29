#pragma once

#include <optional>
#include <vector>

#include "db/access/heap_context.hpp"
#include "db/access/heap_page.hpp"
#include "db/base/identifier.hpp"

namespace db::access {

// Every referenced transaction service must outlive one fetch call. Supplying
// the optional SSI manager selects physical index-fetch semantics: a visible
// heap tuple is protected before an executor can reject the candidate, while
// invisible concurrent versions add read-side dependency edges.
struct HeapFetchContext final {
    HeapMvccContext mvcc{};
    // A fixed global removability horizon enables non-blocking on-access
    // pruning. Absence preserves a strictly read-only fetch.
    std::optional<base::Xid> prune_xmin{};
};

// Owned point-fetch result. No payload view, buffer pin, or content latch
// escapes HeapTable::fetch.
struct HeapFetchTuple final {
    base::Rid rid{base::kInvalidRid};
    TupleHeader header;
    std::vector<std::byte> payload;

    auto operator==(const HeapFetchTuple&) const -> bool = default;
};

}  // namespace db::access
