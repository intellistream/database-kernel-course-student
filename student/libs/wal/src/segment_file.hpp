#pragma once

#include <cstdint>
#include <filesystem>

#include "db/base/result.hpp"
#include "db/io/posix.hpp"
#include "db/wal/format.hpp"

namespace db::wal::segment_file {

struct OpenedSegment final {
    io::PosixFile file;
    std::uint64_t data_length{0};
};

auto make_header(
    std::uint64_t system_identifier,
    std::uint32_t timeline,
    std::uint64_t segment_number
) -> base::Result<WalSegmentHeaderBytes>;
auto open_existing(
    const std::filesystem::path& path,
    std::uint64_t expected_system_identifier,
    std::uint32_t expected_timeline,
    std::uint64_t expected_segment_number
) -> base::Result<OpenedSegment>;

}  // namespace db::wal::segment_file
