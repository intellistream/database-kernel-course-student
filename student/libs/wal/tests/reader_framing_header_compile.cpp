#include <cstddef>
#include <type_traits>
#include <variant>

#include "reader_framing.hpp"

static_assert(std::is_constructible_v<
              db::wal::reader_detail::FramingCursor,
              const db::wal::WalSegmentStorage&,
              db::wal::reader_detail::StreamSnapshot,
              std::size_t>);
static_assert(!std::is_copy_constructible_v<db::wal::reader_detail::FramingCursor>);
static_assert(std::variant_size_v<db::wal::reader_detail::FramingReadResult> == 3U);
