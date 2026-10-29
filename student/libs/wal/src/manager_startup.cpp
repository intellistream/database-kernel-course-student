#include "manager_startup.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/ring_buffer.hpp"
#include "db/wal/segment_storage.hpp"

namespace db::wal::manager_detail {
namespace {

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.manager",
        "validate"
    );
}

}  // namespace

[[maybe_unused]] auto prepare_startup(
    [[maybe_unused]] const std::filesystem::path& directory,
    [[maybe_unused]] const WalManagerOptions& options
) -> base::Result<ManagerStartup> {
    // LAB-S10.Q11:R11-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q11:R11"});
    // LAB-S10.Q11:R11-END
}

}  // namespace db::wal::manager_detail
