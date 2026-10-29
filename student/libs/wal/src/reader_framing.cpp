#include "reader_framing.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "db/base/bytes.hpp"
#include "db/base/crc32c.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/base/result.hpp"
#include "db/wal/format.hpp"
#include "db/wal/record.hpp"

namespace db::wal::reader_detail {
namespace {

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.reader",
        "validate"
    );
}

[[maybe_unused]] auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.reader",
        "decode stream"
    );
}

[[maybe_unused]] auto invariant(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvariantViolation, std::move(message)}.with_context(
        "wal.reader",
        "commit framing"
    );
}

[[maybe_unused]] auto all_zero([[maybe_unused]] const base::Bytes bytes) noexcept -> bool {
    // LAB-S10.Q03:R10-BEGIN
    base::lab::missing_algorithm("S10.Q03:R10");
    // LAB-S10.Q03:R10-END
}

[[maybe_unused]] auto valid_stream_cursor(const std::uint64_t lsn) noexcept -> bool {
    const auto offset = lsn % kWalPageSize;
    return offset == 0 || (offset >= kWalPageHeaderSize && offset % 8U == 0);
}

}  // namespace

[[maybe_unused]] auto inspect_stream_snapshot([[maybe_unused]] const WalSegmentStorage& storage)
    -> base::Result<StreamSnapshot> {
    // LAB-S10.Q03:R11-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R11"});
    // LAB-S10.Q03:R11-END
}

FramingCursor::FramingCursor(
    const WalSegmentStorage& storage,
    const StreamSnapshot snapshot,
    const std::size_t read_ahead_capacity
)
    : storage_{std::addressof(storage)},
      cursor_{snapshot.first_lsn},
      first_{snapshot.first_lsn},
      end_{snapshot.end_lsn},
      guard_incomplete_{snapshot.incomplete_guard},
      read_ahead_{read_ahead_capacity} {}

auto FramingCursor::position() const noexcept -> std::uint64_t {
    return cursor_;
}

auto FramingCursor::stream_start() const noexcept -> std::uint64_t {
    return first_;
}

auto FramingCursor::logical_end() const noexcept -> std::uint64_t {
    return end_;
}

auto FramingCursor::storage_read_count() const noexcept -> std::uint64_t {
    return read_ahead_.fills();
}

[[maybe_unused]] auto FramingCursor::seek([[maybe_unused]] const std::uint64_t lsn)
    -> base::Status {
    // LAB-S10.Q03:R12-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R12"});
    // LAB-S10.Q03:R12-END
}

[[maybe_unused]] auto FramingCursor::validate_page_header(
    [[maybe_unused]] const std::uint64_t page_start,
    [[maybe_unused]] const bool continuation
) -> base::Result<std::optional<IncompleteTail>> {
    // LAB-S10.Q03:R13-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R13"});
    // LAB-S10.Q03:R13-END
}

[[maybe_unused]] auto FramingCursor::consume_record_start_page()
    -> base::Result<std::optional<IncompleteTail>> {
    // LAB-S10.Q03:R14-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R14"});
    // LAB-S10.Q03:R14-END
}

[[maybe_unused]] auto
FramingCursor::consume_page_tail_padding([[maybe_unused]] const std::uint64_t remaining)
    -> base::Result<std::optional<IncompleteTail>> {
    // LAB-S10.Q03:R15-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R15"});
    // LAB-S10.Q03:R15-END
}

[[maybe_unused]] auto FramingCursor::prepare_record_start()
    -> base::Result<FramingCursor::PrepareResult> {
    // LAB-S10.Q03:R16-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R16"});
    // LAB-S10.Q03:R16-END
}

[[maybe_unused]] auto FramingCursor::read_payload(
    [[maybe_unused]] std::uint64_t payload_cursor,
    [[maybe_unused]] base::MutableBytes output
) -> base::Result<FramingCursor::PayloadReadResult> {
    // LAB-S10.Q03:R17-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R17"});
    // LAB-S10.Q03:R17-END
}

[[maybe_unused]] auto FramingCursor::read_next() -> base::Result<FramingReadResult> {
    // LAB-S10.Q03:R18-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R18"});
    // LAB-S10.Q03:R18-END
}

[[maybe_unused]] auto FramingCursor::commit_record(
    [[maybe_unused]] const std::uint64_t start_lsn,
    [[maybe_unused]] const std::uint64_t end_lsn
) -> base::Status {
    // LAB-S10.Q03:R19-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R19"});
    // LAB-S10.Q03:R19-END
}

}  // namespace db::wal::reader_detail
