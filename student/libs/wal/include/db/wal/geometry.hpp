#pragma once

#include <cstdint>

namespace db::wal {

inline constexpr std::uint32_t kWalPageSize = 8U * 1024U;
inline constexpr std::uint64_t kWalSegmentSize = std::uint64_t{16} * 1024U * 1024U;
inline constexpr std::uint32_t kMaximumWalRecordSize = 64U * 1024U * 1024U;

}  // namespace db::wal
