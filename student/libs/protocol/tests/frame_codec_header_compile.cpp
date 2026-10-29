#include <type_traits>

#include "db/protocol/frame_codec.hpp"

namespace {

static_assert(db::protocol::kRmdbV3PrefaceSize == 8U);
static_assert(db::protocol::kRmdbFrameHeaderSize == 8U);
static_assert(
    db::protocol::kMaximumRmdbFramePayloadSize == std::size_t{1024U} * std::size_t{1024U}
);
static_assert(!std::is_copy_constructible_v<db::protocol::FrameDecoder>);
static_assert(std::is_nothrow_move_constructible_v<db::protocol::FrameDecoder>);
static_assert(std::is_nothrow_move_assignable_v<db::protocol::FrameDecoder>);
static_assert(!std::is_copy_constructible_v<db::protocol::FrameEncoder>);
static_assert(std::is_nothrow_move_constructible_v<db::protocol::FrameEncoder>);

}  // namespace
