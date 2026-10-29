#include "ring_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::wal::ring_layout {
namespace {

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.ring_buffer",
        "validate"
    );
}

[[maybe_unused]] auto invariant(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvariantViolation, std::move(message)}.with_context(
        "wal.ring_buffer",
        "maintain ring"
    );
}

[[maybe_unused]] auto missing_algorithm_error(const std::string_view region) -> base::Error {
    return base::Error{
        base::ErrorCode::kUnsupported,
        "WAL ring-buffer algorithm is not implemented"
    }
        .with_context("wal.ring_buffer", std::string{region});
}

[[maybe_unused]] auto checked_add(const std::uint64_t value, const std::uint64_t increment)
    -> base::Result<std::uint64_t> {
    if (increment > std::numeric_limits<std::uint64_t>::max() - value) {
        return base::fail(invalid("WAL LSN arithmetic overflows"));
    }
    return value + increment;
}

[[maybe_unused]] auto normalize_record_start([[maybe_unused]] const std::uint64_t cursor)
    -> base::Result<std::uint64_t> {
    // LAB-S10.Q01:R18-BEGIN
    static_cast<void>(cursor);
    return base::fail(missing_algorithm_error("LAB-S10.Q01:R18"));
    // LAB-S10.Q01:R18-END
}

[[maybe_unused]] auto
advance_payload([[maybe_unused]] std::uint64_t cursor, [[maybe_unused]] std::size_t remaining)
    -> base::Result<std::uint64_t> {
    // LAB-S10.Q01:R19-BEGIN
    static_cast<void>(cursor);
    static_cast<void>(remaining);
    return base::fail(missing_algorithm_error("LAB-S10.Q01:R19"));
    // LAB-S10.Q01:R19-END
}

}  // namespace

auto valid_stream_cursor(const std::uint64_t lsn) noexcept -> bool {
    const auto offset = lsn % kWalPageSize;
    return offset == 0 || (offset >= kWalPageHeaderSize && offset % 8U == 0);
}

[[maybe_unused]] auto make_record_layout(
    [[maybe_unused]] const std::uint64_t buffer_start,
    [[maybe_unused]] const std::size_t payload_size
) -> base::Result<RecordLayout> {
    // LAB-S10.Q01:R20-BEGIN
    static_cast<void>(buffer_start);
    static_cast<void>(payload_size);
    return base::fail(missing_algorithm_error("LAB-S10.Q01:R20"));
    // LAB-S10.Q01:R20-END
}

[[maybe_unused]] auto page_span([[maybe_unused]] const RecordLayout& layout)
    -> base::Result<std::size_t> {
    // LAB-S10.Q01:R21-BEGIN
    static_cast<void>(layout);
    return base::fail(missing_algorithm_error("LAB-S10.Q01:R21"));
    // LAB-S10.Q01:R21-END
}

auto make_page_header(const std::uint64_t page_number, const bool continuation)
    -> base::Result<WalPageHeaderBytes> {
    if (page_number > std::numeric_limits<std::uint64_t>::max() / kWalPageSize) {
        return base::fail(invalid("WAL page number overflows its starting LSN"));
    }
    const auto page_start = page_number * kWalPageSize;
    auto encoded = encode_page_header(
        WalPageHeader{
            .page_start_lsn = base::Lsn{page_start},
            .segment_number = page_start / kWalSegmentSize,
            .flags = continuation ? kWalPageContinuation : std::uint16_t{0},
            .checksum = 0,
        }
    );
    if (!encoded) {
        return base::fail(std::move(encoded).error());
    }
    if (auto status = base::seal_crc32c(encoded.value(), kWalPageChecksumOffset); !status) {
        return base::fail(std::move(status).error());
    }
    return encoded.value();
}

[[maybe_unused]] auto materialize_oversized(
    [[maybe_unused]] const std::uint64_t buffer_start,
    [[maybe_unused]] const std::uint64_t record_start,
    [[maybe_unused]] const std::uint64_t record_end,
    [[maybe_unused]] const base::Bytes payload
) -> base::Result<std::vector<std::byte>> {
    // LAB-S10.Q01:R22-BEGIN
    static_cast<void>(buffer_start);
    static_cast<void>(record_start);
    static_cast<void>(record_end);
    static_cast<void>(payload);
    return base::fail(missing_algorithm_error("LAB-S10.Q01:R22"));
    // LAB-S10.Q01:R22-END
}

}  // namespace db::wal::ring_layout
