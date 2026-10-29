#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/result.hpp"

namespace db::base {

using Bytes = std::span<const std::byte>;
using MutableBytes = std::span<std::byte>;

namespace detail {

enum class ByteOrder : std::uint8_t {
    kLittleEndian,
    kBigEndian,
};

template <ByteOrder Order>
class ByteReader final {
public:
    constexpr explicit ByteReader(const Bytes input) noexcept
        : input_{input} {}

    constexpr auto position() const noexcept -> std::size_t {
        return position_;
    }

    constexpr auto remaining() const noexcept -> std::size_t {
        return input_.size() - position_;
    }

    constexpr auto empty() const noexcept -> bool {
        return remaining() == 0;
    }

    auto read_u8() -> Result<std::uint8_t> {
        return read_unsigned<std::uint8_t>();
    }

    auto read_u16() -> Result<std::uint16_t> {
        return read_unsigned<std::uint16_t>();
    }

    auto read_u32() -> Result<std::uint32_t> {
        return read_unsigned<std::uint32_t>();
    }

    auto read_u64() -> Result<std::uint64_t> {
        return read_unsigned<std::uint64_t>();
    }

    auto read_i32() -> Result<std::int32_t> {
        Result<std::uint32_t> encoded = read_u32();
        if (!encoded) {
            return fail(std::move(encoded).error());
        }
        return std::bit_cast<std::int32_t>(std::move(encoded).value());
    }

    auto read_i64() -> Result<std::int64_t> {
        Result<std::uint64_t> encoded = read_u64();
        if (!encoded) {
            return fail(std::move(encoded).error());
        }
        return std::bit_cast<std::int64_t>(std::move(encoded).value());
    }

    auto read_bytes(const std::size_t size) -> Result<Bytes> {
        if (size > remaining()) {
            return fail(Error{ErrorCode::kCorruption, "byte input is truncated"});
        }
        const Bytes value = input_.subspan(position_, size);
        position_ += size;
        return value;
    }

    auto finish() const -> Status {
        if (!empty()) {
            return fail(Error{ErrorCode::kCorruption, "byte input has trailing data"});
        }
        return {};
    }

private:
    template <std::unsigned_integral Value>
    auto read_unsigned() -> Result<Value> {
        constexpr std::size_t width = sizeof(Value);
        if (width > remaining()) {
            return fail(Error{ErrorCode::kCorruption, "byte input is truncated"});
        }

        Value value = 0;
        if constexpr (Order == ByteOrder::kLittleEndian) {
            for (std::size_t index = 0; index < width; ++index) {
                const auto byte = std::to_integer<Value>(input_[position_ + index]);
                value |= static_cast<Value>(byte << (index * 8U));
            }
        } else {
            for (std::size_t index = 0; index < width; ++index) {
                value = static_cast<Value>(value << 8U);
                value |= std::to_integer<Value>(input_[position_ + index]);
            }
        }
        position_ += width;
        return value;
    }

    Bytes input_;
    std::size_t position_{0};
};

template <ByteOrder Order>
class ByteWriter final {
public:
    constexpr explicit ByteWriter(const MutableBytes output) noexcept
        : output_{output} {}

    constexpr auto position() const noexcept -> std::size_t {
        return position_;
    }

    constexpr auto remaining() const noexcept -> std::size_t {
        return output_.size() - position_;
    }

    constexpr auto written() const noexcept -> Bytes {
        return output_.first(position_);
    }

    auto write_u8(const std::uint8_t value) -> Status {
        return write_unsigned(value);
    }

    auto write_u16(const std::uint16_t value) -> Status {
        return write_unsigned(value);
    }

    auto write_u32(const std::uint32_t value) -> Status {
        return write_unsigned(value);
    }

    auto write_u64(const std::uint64_t value) -> Status {
        return write_unsigned(value);
    }

    auto write_i32(const std::int32_t value) -> Status {
        return write_u32(std::bit_cast<std::uint32_t>(value));
    }

    auto write_i64(const std::int64_t value) -> Status {
        return write_u64(std::bit_cast<std::uint64_t>(value));
    }

    auto write_bytes(const Bytes value) -> Status {
        if (value.size() > remaining()) {
            return fail(Error{ErrorCode::kOutOfRange, "byte output buffer is too small"});
        }
        for (const std::byte byte : value) {
            output_[position_] = byte;
            ++position_;
        }
        return {};
    }

private:
    template <std::unsigned_integral Value>
    auto write_unsigned(const Value value) -> Status {
        constexpr std::size_t width = sizeof(Value);
        if (width > remaining()) {
            return fail(Error{ErrorCode::kOutOfRange, "byte output buffer is too small"});
        }

        for (std::size_t index = 0; index < width; ++index) {
            const std::size_t shift =
                Order == ByteOrder::kLittleEndian ? index * 8U : (width - index - 1U) * 8U;
            output_[position_ + index] =
                static_cast<std::byte>(static_cast<std::uint8_t>(value >> shift));
        }
        position_ += width;
        return {};
    }

    MutableBytes output_;
    std::size_t position_{0};
};

// Appends fixed-width values to caller-owned dynamic storage. Byte order is
// explicit and the vector retains ownership; allocation failure follows the
// vector operation and must be translated at an outer API/protocol boundary.
template <ByteOrder Order>
class ByteAppender final {
public:
    explicit ByteAppender(std::vector<std::byte>& output) noexcept
        : output_{output} {}

    ByteAppender(const ByteAppender&) = delete;
    auto operator=(const ByteAppender&) -> ByteAppender& = delete;
    ByteAppender(ByteAppender&&) = delete;
    auto operator=(ByteAppender&&) -> ByteAppender& = delete;
    ~ByteAppender() = default;

    void append_u8(const std::uint8_t value) {
        append_unsigned(value);
    }

    void append_u16(const std::uint16_t value) {
        append_unsigned(value);
    }

    void append_u32(const std::uint32_t value) {
        append_unsigned(value);
    }

    void append_u64(const std::uint64_t value) {
        append_unsigned(value);
    }

    void append_i32(const std::int32_t value) {
        append_u32(std::bit_cast<std::uint32_t>(value));
    }

    void append_i64(const std::int64_t value) {
        append_u64(std::bit_cast<std::uint64_t>(value));
    }

    void append_bytes(const Bytes value) {
        output_.insert(output_.end(), value.begin(), value.end());
    }

private:
    template <std::unsigned_integral Value>
    void append_unsigned(const Value value) {
        constexpr std::size_t width = sizeof(Value);
        for (std::size_t index = 0; index < width; ++index) {
            const std::size_t shift =
                Order == ByteOrder::kLittleEndian ? index * 8U : (width - index - 1U) * 8U;
            output_.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(value >> shift)));
        }
    }

    std::vector<std::byte>& output_;
};

}  // namespace detail

using LittleEndianReader = detail::ByteReader<detail::ByteOrder::kLittleEndian>;
using BigEndianReader = detail::ByteReader<detail::ByteOrder::kBigEndian>;
using LittleEndianWriter = detail::ByteWriter<detail::ByteOrder::kLittleEndian>;
using BigEndianWriter = detail::ByteWriter<detail::ByteOrder::kBigEndian>;
using LittleEndianAppender = detail::ByteAppender<detail::ByteOrder::kLittleEndian>;
using BigEndianAppender = detail::ByteAppender<detail::ByteOrder::kBigEndian>;

}  // namespace db::base
