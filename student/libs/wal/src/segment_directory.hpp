#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "db/base/result.hpp"

namespace db::wal::segment_directory {

auto filename(std::uint64_t segment_number) -> std::string;
auto path(const std::filesystem::path& directory, std::uint64_t segment_number)
    -> std::filesystem::path;
auto list_numbers(const std::filesystem::path& directory)
    -> base::Result<std::vector<std::uint64_t>>;

}  // namespace db::wal::segment_directory
