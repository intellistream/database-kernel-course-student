#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "db/base/bytes.hpp"

namespace db::protocol::internal {

// Append an unsigned integer in RMDB network byte order. Payload size policy
// remains with the request or response codec so this helper cannot silently
// choose an error category or protocol limit for its caller.
template <std::unsigned_integral Value>
void append_big_endian(std::vector<std::byte>& output, const Value value) {
    static_assert(
        std::same_as<Value, std::uint8_t> || std::same_as<Value, std::uint16_t>
        || std::same_as<Value, std::uint32_t> || std::same_as<Value, std::uint64_t>
    );
    base::BigEndianAppender appender{output};
    if constexpr (std::same_as<Value, std::uint8_t>) {
        appender.append_u8(value);
    } else if constexpr (std::same_as<Value, std::uint16_t>) {
        appender.append_u16(value);
    } else if constexpr (std::same_as<Value, std::uint32_t>) {
        appender.append_u32(value);
    } else {
        appender.append_u64(value);
    }
}

inline void append_bytes(std::vector<std::byte>& output, const base::Bytes bytes) {
    base::BigEndianAppender{output}.append_bytes(bytes);
}

inline void append_text_bytes(std::vector<std::byte>& output, const std::string_view text) {
    append_bytes(output, std::as_bytes(std::span{text.data(), text.size()}));
}

}  // namespace db::protocol::internal
