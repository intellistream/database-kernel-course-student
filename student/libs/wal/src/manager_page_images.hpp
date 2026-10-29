#pragma once

#include <span>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/record.hpp"

namespace db::wal::manager_detail {

auto apply_page_image_policy(
    WalRecord source,
    std::span<const WalPageImageRegistration> registrations,
    base::Lsn redo_lsn
) -> base::Result<WalRecord>;

}  // namespace db::wal::manager_detail
