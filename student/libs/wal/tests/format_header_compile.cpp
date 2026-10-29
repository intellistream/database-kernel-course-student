#include "db/wal/format.hpp"

namespace {

static_assert(db::wal::kWalFormatVersion == 1);
static_assert(db::wal::kWalPageSize == 8192);
static_assert(db::wal::kWalSegmentSize == std::uint64_t{16} * 1024U * 1024U);
static_assert(db::wal::kWalControlFileSize == 512);

}  // namespace
