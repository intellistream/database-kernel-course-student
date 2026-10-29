#include "manager_page_images.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::wal::manager_detail {
namespace {

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.manager",
        "validate"
    );
}

}  // namespace

[[maybe_unused]] auto apply_page_image_policy(
    [[maybe_unused]] WalRecord source,
    [[maybe_unused]] const std::span<const WalPageImageRegistration> registrations,
    [[maybe_unused]] const base::Lsn redo_lsn
) -> base::Result<WalRecord> {
    // LAB-S10.Q05:R17-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R17"});
    // LAB-S10.Q05:R17-END
}

}  // namespace db::wal::manager_detail
