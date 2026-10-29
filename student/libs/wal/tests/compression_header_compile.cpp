#include "db/wal/compression.hpp"

namespace {

static_assert(static_cast<unsigned>(db::wal::CompressionMethod::kNone) == 0);
static_assert(static_cast<unsigned>(db::wal::CompressionMethod::kRle) == 1);
static_assert(db::wal::kWalCompressionMinimumSavings == 8);

}  // namespace
