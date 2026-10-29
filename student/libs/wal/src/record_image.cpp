#include "record_image.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/wal/format.hpp"

namespace db::wal::record_image {
namespace {

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)}.with_context(
        "wal.record",
        "encode"
    );
}

[[maybe_unused]] auto corrupt(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "wal.record",
        "decode"
    );
}

[[maybe_unused]] auto valid_hole(
    [[maybe_unused]] const WalPageImageHole hole,
    [[maybe_unused]] const bool standard_page
) noexcept -> bool {
    // LAB-S10.Q05:R18-BEGIN
    base::lab::missing_algorithm("S10.Q05:R18");
    // LAB-S10.Q05:R18-END
}

}  // namespace

[[maybe_unused]] auto encode(
    [[maybe_unused]] const WalPageImage& image,
    [[maybe_unused]] const WalPageImageHole hole,
    [[maybe_unused]] const bool standard_page
) -> base::Result<EncodedImage> {
    // LAB-S10.Q05:R19-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R19"});
    // LAB-S10.Q05:R19-END
}

[[maybe_unused]] auto validate_metadata(
    [[maybe_unused]] const CompressionMethod compression,
    [[maybe_unused]] const std::size_t encoded_length,
    [[maybe_unused]] const WalPageImageHole hole,
    [[maybe_unused]] const bool standard_page
) -> base::Status {
    // LAB-S10.Q05:R20-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R20"});
    // LAB-S10.Q05:R20-END
}

[[maybe_unused]] auto decode(
    [[maybe_unused]] const base::Bytes bytes,
    [[maybe_unused]] const CompressionMethod compression,
    [[maybe_unused]] const WalPageImageHole hole,
    [[maybe_unused]] const bool standard_page
) -> base::Result<WalPageImage> {
    // LAB-S10.Q05:R21-BEGIN

    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q05:R21"});
    // LAB-S10.Q05:R21-END
}

}  // namespace db::wal::record_image
