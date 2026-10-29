#include <cstdint>
#include <string>
#include <type_traits>

#include "segment_directory.hpp"

static_assert(
    std::is_same_v<decltype(&db::wal::segment_directory::filename), std::string (*)(std::uint64_t)>
);
