#include "db/access/btree_wal.hpp"

namespace {

static_assert(sizeof(db::access::BtreeWalOperation) == 1U);

}  // namespace
