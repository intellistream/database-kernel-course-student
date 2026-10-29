#include "btree_logging.hpp"

#include <string>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::access::detail {
namespace {

[[maybe_unused]] auto logging_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvariantViolation, std::move(message)}.with_context(
        "access",
        "log B-tree mutation"
    );
}

[[maybe_unused]] void
restore_before_images(const std::span<const BtreeWalPageMutation> pages) noexcept {
    for (const auto& page : pages) {
        if (page.before_page != nullptr && page.after_page != nullptr) {
            *page.after_page = *page.before_page;
        }
    }
}

}  // namespace

[[maybe_unused]] auto log_btree_mutation(
    [[maybe_unused]] BtreeMutationLogPort& mutation_log,
    [[maybe_unused]] const BtreeWalOperation operation,
    [[maybe_unused]] const base::Xid xid,
    [[maybe_unused]] const std::span<const BtreeWalPageMutation> pages
) -> base::Status {
    // LAB-S10.Q06:R03-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q06:R03"});
    // LAB-S10.Q06:R03-END
}

}  // namespace db::access::detail
