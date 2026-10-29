#pragma once

#include <cstddef>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/wal/compression.hpp"
#include "db/wal/record.hpp"

namespace db::wal::record_image {

struct EncodedImage final {
    CompressionMethod compression{CompressionMethod::kNone};
    std::vector<std::byte> bytes;
    WalPageImageHole hole{};
};

auto encode(const WalPageImage& image, WalPageImageHole hole, bool standard_page)
    -> base::Result<EncodedImage>;
auto validate_metadata(
    CompressionMethod compression,
    std::size_t encoded_length,
    WalPageImageHole hole,
    bool standard_page
) -> base::Status;
auto decode(
    base::Bytes bytes,
    CompressionMethod compression,
    WalPageImageHole hole,
    bool standard_page
) -> base::Result<WalPageImage>;

}  // namespace db::wal::record_image
