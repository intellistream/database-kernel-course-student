#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/wal/format.hpp"
#include "db/wal/segment_storage.hpp"

#include "reader_read_ahead.hpp"

namespace db::wal::reader_detail {

struct StreamSnapshot final {
    std::uint64_t first_lsn{0};
    std::uint64_t end_lsn{0};
    bool incomplete_guard{false};
};

struct IncompleteTail final {
    std::uint64_t lsn{0};
    std::string part;
};

struct FramedRecordView final {
    std::uint64_t start_lsn{0};
    std::uint64_t end_lsn{0};
    base::Bytes bytes;
    WalRecordHeader header;
};

struct EndOfStream final {};

using FramingReadResult = std::variant<FramedRecordView, EndOfStream, IncompleteTail>;

auto inspect_stream_snapshot(const WalSegmentStorage& storage) -> base::Result<StreamSnapshot>;

// Owns the physical cursor and scratch buffer for one immutable storage
// snapshot. A record remains uncommitted until its caller accepts the decoded
// logical record, so corruption never advances the public reader position.
class FramingCursor final {
public:
    FramingCursor(
        const WalSegmentStorage& storage,
        StreamSnapshot snapshot,
        std::size_t read_ahead_capacity
    );
    ~FramingCursor() = default;

    FramingCursor(const FramingCursor&) = delete;
    auto operator=(const FramingCursor&) -> FramingCursor& = delete;
    FramingCursor(FramingCursor&&) = delete;
    auto operator=(FramingCursor&&) -> FramingCursor& = delete;

    auto position() const noexcept -> std::uint64_t;
    auto stream_start() const noexcept -> std::uint64_t;
    auto logical_end() const noexcept -> std::uint64_t;
    auto storage_read_count() const noexcept -> std::uint64_t;

    auto seek(std::uint64_t lsn) -> base::Status;
    auto read_next() -> base::Result<FramingReadResult>;
    auto commit_record(std::uint64_t start_lsn, std::uint64_t end_lsn) -> base::Status;

private:
    enum class PrepareKind : std::uint8_t {
        kReady,
        kEnd,
        kIncomplete,
    };

    struct PrepareResult final {
        PrepareKind kind{PrepareKind::kReady};
        std::optional<IncompleteTail> incomplete;
    };

    struct PayloadReadResult final {
        std::uint64_t end_lsn{0};
        std::optional<IncompleteTail> incomplete;
    };

    auto validate_page_header(std::uint64_t page_start, bool continuation)
        -> base::Result<std::optional<IncompleteTail>>;
    auto consume_record_start_page() -> base::Result<std::optional<IncompleteTail>>;
    auto consume_page_tail_padding(std::uint64_t remaining)
        -> base::Result<std::optional<IncompleteTail>>;
    auto prepare_record_start() -> base::Result<PrepareResult>;
    auto read_payload(std::uint64_t payload_cursor, base::MutableBytes output)
        -> base::Result<PayloadReadResult>;

    const WalSegmentStorage* storage_;
    std::uint64_t cursor_;
    std::uint64_t first_;
    std::uint64_t end_;
    bool guard_incomplete_;
    std::vector<std::byte> record_buffer_;
    ReadAhead read_ahead_;
};

}  // namespace db::wal::reader_detail
