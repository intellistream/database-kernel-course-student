#include <type_traits>

#include "record_image.hpp"

static_assert(std::is_same_v<
              decltype(db::wal::record_image::EncodedImage::compression),
              db::wal::CompressionMethod>);
