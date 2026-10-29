#include "segment_directory.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::wal::segment_directory {
namespace {

inline constexpr std::string_view kSegmentExtension = ".wal";

[[maybe_unused]] auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.segment_storage",
        "open"
    );
}

[[maybe_unused]] auto filesystem_error(std::string message, const std::error_code& system_error)
    -> base::Error {
    return base::Error{base::ErrorCode::kIo, std::move(message), system_error}.with_context(
        "wal.segment_storage",
        "scan directory"
    );
}

[[maybe_unused]] auto parse_filename(
    [[maybe_unused]] const std::string_view candidate,
    [[maybe_unused]] std::uint64_t& segment_number
) noexcept -> bool {
    // LAB-S10.Q02:R01-BEGIN
    base::lab::missing_algorithm("S10.Q02:R01");
    // LAB-S10.Q02:R01-END
}

}  // namespace

[[maybe_unused]] auto filename([[maybe_unused]] std::uint64_t segment_number) -> std::string {
    // LAB-S10.Q02:R02-BEGIN
    base::lab::missing_algorithm("S10.Q02:R02");
    // LAB-S10.Q02:R02-END
}

[[maybe_unused]] auto path(
    [[maybe_unused]] const std::filesystem::path& directory,
    [[maybe_unused]] const std::uint64_t segment_number
) -> std::filesystem::path {
    // LAB-S10.Q02:R03-BEGIN
    base::lab::missing_algorithm("S10.Q02:R03");
    // LAB-S10.Q02:R03-END
}

[[maybe_unused]] auto list_numbers([[maybe_unused]] const std::filesystem::path& directory)
    -> base::Result<std::vector<std::uint64_t>> {
    // LAB-S10.Q02:R04-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q02:R04"});
    // LAB-S10.Q02:R04-END
}

}  // namespace db::wal::segment_directory
