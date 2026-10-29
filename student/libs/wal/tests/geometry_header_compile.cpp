#include <cstdint>

#include "db/wal/geometry.hpp"

static_assert(db::wal::kWalPageSize == 8192U);
static_assert(db::wal::kWalSegmentSize == std::uint64_t{16} * 1024U * 1024U);
static_assert(db::wal::kMaximumWalRecordSize == 64U * 1024U * 1024U);
