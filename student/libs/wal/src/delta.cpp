#include "db/wal/delta.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::wal {
namespace {

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.delta",
        "encode/apply"
    );
}

[[maybe_unused]] auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.delta",
        "decode"
    );
}

[[maybe_unused]] auto validate_patches(
    [[maybe_unused]] const std::span<const PageDeltaPatch> patches,
    [[maybe_unused]] const bool decoding
) -> base::Status {
    // LAB-S10.Q05:R04-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R04"});
    // LAB-S10.Q05:R04-END
}

}  // namespace

[[maybe_unused]] auto PageDelta::between(
    [[maybe_unused]] const base::Bytes before,
    [[maybe_unused]] const base::Bytes after
) -> base::Result<PageDelta> {
    // LAB-S10.Q05:R05-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R05"});
    // LAB-S10.Q05:R05-END
}

[[maybe_unused]] auto
PageDelta::create([[maybe_unused]] const std::span<const PageDeltaPatch> patches)
    -> base::Result<PageDelta> {
    // LAB-S10.Q05:R06-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R06"});
    // LAB-S10.Q05:R06-END
}

[[maybe_unused]] auto PageDelta::encode() const -> base::Result<std::vector<std::byte>> {
    // LAB-S10.Q05:R07-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R07"});
    // LAB-S10.Q05:R07-END
}

[[maybe_unused]] auto PageDelta::decode([[maybe_unused]] const base::Bytes bytes)
    -> base::Result<PageDelta> {
    // LAB-S10.Q05:R08-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R08"});
    // LAB-S10.Q05:R08-END
}

[[maybe_unused]] auto PageDelta::apply([[maybe_unused]] const base::MutableBytes page) const
    -> base::Status {
    // LAB-S10.Q05:R09-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R09"});
    // LAB-S10.Q05:R09-END
}

[[maybe_unused]] auto
make_page_delta([[maybe_unused]] const base::Bytes before, [[maybe_unused]] const base::Bytes after)
    -> base::Result<PageDelta> {
    // LAB-S10.Q05:R10-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R10"});
    // LAB-S10.Q05:R10-END
}

[[maybe_unused]] auto encode_page_delta([[maybe_unused]] const PageDelta& delta)
    -> base::Result<std::vector<std::byte>> {
    // LAB-S10.Q05:R11-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R11"});
    // LAB-S10.Q05:R11-END
}

[[maybe_unused]] auto
encode_page_delta([[maybe_unused]] const std::span<const PageDeltaPatch> patches)
    -> base::Result<std::vector<std::byte>> {
    // LAB-S10.Q05:R12-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R12"});
    // LAB-S10.Q05:R12-END
}

[[maybe_unused]] auto decode_page_delta([[maybe_unused]] const base::Bytes bytes)
    -> base::Result<PageDelta> {
    // LAB-S10.Q05:R13-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R13"});
    // LAB-S10.Q05:R13-END
}

[[maybe_unused]] auto apply_page_delta(
    [[maybe_unused]] const base::MutableBytes page,
    [[maybe_unused]] const PageDelta& delta
) -> base::Status {
    // LAB-S10.Q05:R14-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R14"});
    // LAB-S10.Q05:R14-END
}

[[maybe_unused]] auto apply_page_delta(
    [[maybe_unused]] const base::MutableBytes page,
    [[maybe_unused]] const std::span<const PageDeltaPatch> patches
) -> base::Status {
    // LAB-S10.Q05:R15-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R15"});
    // LAB-S10.Q05:R15-END
}

}  // namespace db::wal
