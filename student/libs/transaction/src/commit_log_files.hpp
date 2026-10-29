#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "db/base/result.hpp"

namespace db::transaction {

using CommitLogDiscoveredSegments = std::map<std::uint32_t, std::filesystem::path>;

auto commit_log_segment_file_name(std::uint32_t segment_number) -> std::string;

auto commit_log_directory_is_empty(const std::filesystem::path& directory) -> base::Result<bool>;

auto discover_commit_log_segments(const std::filesystem::path& directory)
    -> base::Result<CommitLogDiscoveredSegments>;

auto read_complete_commit_log_file(const std::filesystem::path& path, std::size_t expected_size)
    -> base::Result<std::vector<std::byte>>;

}  // namespace db::transaction
