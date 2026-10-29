#include "db/wal/delta.hpp"

namespace {

static_assert(db::wal::kWalDataPageSize == 4096);
static_assert(sizeof(db::wal::WalPageImage) == db::wal::kWalDataPageSize);

}  // namespace
