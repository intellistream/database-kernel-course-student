#include <type_traits>

#include "db/protocol/response_encoder.hpp"

static_assert(!std::is_copy_constructible_v<db::protocol::ResponseSink>);
static_assert(!std::is_copy_constructible_v<db::protocol::ResponseEncoder>);
static_assert(std::is_nothrow_move_constructible_v<db::protocol::ResponseEncoder>);

namespace {

[[maybe_unused]] void compile_response_encoder_api() {
    db::protocol::ResponseEncoder encoder;
    static_cast<void>(encoder.frame_buffer_capacity());
    static_cast<void>(encoder.payload_buffer_capacity());
}

}  // namespace
