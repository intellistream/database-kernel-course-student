#pragma once

#include <cstddef>

#include "db/access/btree_key.hpp"
#include "db/base/result.hpp"
#include "db/recovery/btree_wal.hpp"
#include "db/recovery/dispatcher.hpp"
#include "db/storage/page.hpp"
#include "db/wal/reader.hpp"

namespace db::recovery::detail {

// The caller supplies a dispatcher result obtained from this exact decoded
// record. Keeping this internal prevents a caller-constructed dispatch plan
// from becoming part of the public page-local B-tree redo contract.
auto apply_btree_redo_dispatched(
    const wal::DecodedWalRecord& decoded,
    const RecoveryRecordDispatch& dispatch,
    std::size_t block_index,
    storage::Page& page
) -> base::Result<BtreeRedoDisposition>;

}  // namespace db::recovery::detail
