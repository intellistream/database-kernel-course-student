#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/wal/format.hpp"

namespace db::wal::ring_layout {

struct RecordLayout final {
    std::uint64_t buffer_start;
    std::uint64_t record_start;
    std::uint64_t record_end;
    std::uint64_t first_page;
    std::uint64_t last_page;
    std::size_t payload_size;
};

auto valid_stream_cursor(std::uint64_t lsn) noexcept -> bool;
auto make_record_layout(std::uint64_t buffer_start, std::size_t payload_size)
    -> base::Result<RecordLayout>;
auto page_span(const RecordLayout& layout) -> base::Result<std::size_t>;
auto make_page_header(std::uint64_t page_number, bool continuation)
    -> base::Result<WalPageHeaderBytes>;
auto materialize_oversized(
    std::uint64_t buffer_start,
    std::uint64_t record_start,
    std::uint64_t record_end,
    base::Bytes payload
) -> base::Result<std::vector<std::byte>>;

}  // namespace db::wal::ring_layout
