#pragma once

#include <span>

#include "db/access/btree_wal.hpp"
#include "db/base/result.hpp"

namespace db::access::detail {

auto log_btree_mutation(
    BtreeMutationLogPort& mutation_log,
    BtreeWalOperation operation,
    base::Xid xid,
    std::span<const BtreeWalPageMutation> pages
) -> base::Status;

}  // namespace db::access::detail
