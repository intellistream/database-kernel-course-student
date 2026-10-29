#include "reader_read_ahead.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::wal::reader_detail {

ReadAhead::ReadAhead(const std::size_t capacity)
    : bytes_(capacity) {}

[[maybe_unused]] auto ReadAhead::read_exact(
    [[maybe_unused]] const WalSegmentStorage& storage,
    [[maybe_unused]] const std::uint64_t file_end,
    [[maybe_unused]] std::uint64_t offset,
    [[maybe_unused]] base::MutableBytes output
) -> base::Status {
    // LAB-S10.Q03:R20-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R20"});
    // LAB-S10.Q03:R20-END
}

auto ReadAhead::fills() const noexcept -> std::uint64_t {
    return fills_;
}

}  // namespace db::wal::reader_detail
