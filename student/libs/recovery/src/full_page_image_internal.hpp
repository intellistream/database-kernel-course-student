#pragma once

#include "db/base/result.hpp"
#include "db/storage/page.hpp"
#include "db/wal/record.hpp"

namespace db::recovery::detail {

// Builds an uncommitted candidate from a decoded WAL image. The caller owns
// resource-manager layout validation, optional following-delta application,
// record-end LSN installation, and publication. WAL images retain the opaque
// legacy page checksum field captured from an in-memory dirty page.
auto make_full_page_image_base(const wal::WalBlockReference& block, storage::PageKind expected_kind)
    -> base::Result<storage::Page>;

}  // namespace db::recovery::detail
