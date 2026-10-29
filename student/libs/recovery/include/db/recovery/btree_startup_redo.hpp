#pragma once

#include <cstddef>

#include "db/base/result.hpp"
#include "db/recovery/startup_page_access.hpp"

namespace db::wal {
struct DecodedWalRecord;
}  // namespace db::wal

namespace db::recovery {

// One B-tree WAL record can mutate multiple pages. Counts distinguish page
// blocks whose normal delta was already durable from blocks that published a
// new candidate during this invocation.
struct BtreeStartupRedoResult final {
    std::size_t applied_blocks{0U};
    std::size_t skipped_blocks{0U};

    auto operator==(const BtreeStartupRedoResult&) const noexcept -> bool = default;
};

// Replays one B-tree-routed decoded WAL record through caller-owned startup
// page access. Semantic WAL dispatch completes before the first acquire();
// each block then uses its existing-page or replacement load mode
// and becomes dirty only after its page-local redo candidate publishes.
auto replay_btree_startup_redo(const wal::DecodedWalRecord& decoded, StartupPageAccessPort& pages)
    -> base::Result<BtreeStartupRedoResult>;

}  // namespace db::recovery
