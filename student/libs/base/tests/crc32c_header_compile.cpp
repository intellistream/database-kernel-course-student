#include <cstdint>

#include "db/base/crc32c.hpp"

namespace {

using ChecksumFunction = std::uint32_t (*)(db::base::Bytes) noexcept;
static_assert(db::base::kCrc32cSize == sizeof(std::uint32_t));
[[maybe_unused]] constexpr ChecksumFunction kChecksum = &db::base::crc32c;

}  // namespace
