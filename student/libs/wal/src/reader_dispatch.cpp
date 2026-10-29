#include "reader_dispatch.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::wal::reader_detail {
namespace {

[[maybe_unused]] auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.reader",
        "decode stream"
    );
}

[[maybe_unused]] auto page_address_key(const WalBlockReference& block) noexcept -> std::uint64_t {
    return (static_cast<std::uint64_t>(block.relation_id.value()) << 32U) | block.page_id.value();
}

[[maybe_unused]] auto known_opcode(
    [[maybe_unused]] const WalResourceManager manager,
    [[maybe_unused]] const std::uint8_t info
) noexcept -> bool {
    // LAB-S10.Q03:R08-BEGIN
    base::lab::missing_algorithm("S10.Q03:R08");
    // LAB-S10.Q03:R08-END
}

}  // namespace

[[maybe_unused]] auto validate_dispatch_shape([[maybe_unused]] const WalRecord& record)
    -> base::Status {
    // LAB-S10.Q03:R09-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q03:R09"});
    // LAB-S10.Q03:R09-END
}

}  // namespace db::wal::reader_detail
