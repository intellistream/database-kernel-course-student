#include <cstdint>
#include <type_traits>

#include "segment_file.hpp"

static_assert(
    std::is_same_v<decltype(db::wal::segment_file::OpenedSegment::data_length), std::uint64_t>
);
