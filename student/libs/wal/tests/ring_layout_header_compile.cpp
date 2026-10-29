#include <cstdint>
#include <type_traits>

#include "ring_layout.hpp"

static_assert(
    std::is_same_v<decltype(db::wal::ring_layout::RecordLayout::record_start), std::uint64_t>
);
