#pragma once

#include "db/base/result.hpp"
#include "db/storage/page.hpp"
#include "db/wal/manager.hpp"

namespace db::recovery::detail {

// Converts a validated storage page into the generic borrowed image contract
// consumed by WalManager. The page must outlive the insertion call.
auto make_page_image_registration(
    const storage::Page& page,
    bool standard_page,
    wal::WalPageImagePolicy policy,
    bool keep_delta
) -> base::Result<wal::WalPageImageRegistration>;

}  // namespace db::recovery::detail
