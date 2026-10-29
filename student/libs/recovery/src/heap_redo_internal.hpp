#pragma once

#include "db/base/result.hpp"
#include "db/recovery/dispatcher.hpp"
#include "db/recovery/heap_wal.hpp"
#include "db/storage/page.hpp"
#include "db/wal/reader.hpp"

namespace db::recovery::detail {

// The caller supplies a dispatcher result obtained from this exact decoded
// record. Keeping this internal avoids making a caller-constructed dispatch
// plan part of the public page-local redo contract.
auto apply_heap_redo_dispatched(
    const wal::DecodedWalRecord& decoded,
    const RecoveryRecordDispatch& dispatch,
    storage::Page& page
) -> base::Result<HeapRedoDisposition>;

}  // namespace db::recovery::detail
