#include "db/wal/record.hpp"

namespace {

static_assert(db::wal::kMaximumWalBlockReferences == 256);
static_assert((db::wal::kWalBlockKnownFlags & db::wal::kWalBlockHasImage) != 0);
static_assert(db::wal::WalPageImageHole{}.empty());

}  // namespace
