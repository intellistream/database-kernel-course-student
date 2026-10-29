#include "db/protocol/frame_codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"

namespace db::protocol {
namespace {

auto protocol_error(
    const base::ErrorCode code,
    std::string message,
    const std::string_view operation
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "protocol.frame",
        std::string{operation}
    );
}

auto handshake_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "protocol.handshake",
        "validate preface"
    );
}

auto known_frame_tag(const std::uint8_t tag) noexcept -> std::optional<FrameTag> {
    switch (tag) {
        case static_cast<std::uint8_t>(FrameTag::kMeta):
            return FrameTag::kMeta;
        case static_cast<std::uint8_t>(FrameTag::kRow):
            return FrameTag::kRow;
        case static_cast<std::uint8_t>(FrameTag::kCommandOk):
            return FrameTag::kCommandOk;
        case static_cast<std::uint8_t>(FrameTag::kResultEnd):
            return FrameTag::kResultEnd;
        case static_cast<std::uint8_t>(FrameTag::kTransactionAbort):
            return FrameTag::kTransactionAbort;
        case static_cast<std::uint8_t>(FrameTag::kError):
            return FrameTag::kError;
        case static_cast<std::uint8_t>(FrameTag::kPrepareOk):
            return FrameTag::kPrepareOk;
        case static_cast<std::uint8_t>(FrameTag::kBatchResult):
            return FrameTag::kBatchResult;
        case static_cast<std::uint8_t>(FrameTag::kExecStream):
            return FrameTag::kExecStream;
        case static_cast<std::uint8_t>(FrameTag::kPrepareSet):
            return FrameTag::kPrepareSet;
        case static_cast<std::uint8_t>(FrameTag::kExecBatch):
            return FrameTag::kExecBatch;
    }
    return std::nullopt;
}

auto tag_allows_direction(const FrameDirection direction, const FrameTag tag) noexcept -> bool {
    switch (direction) {
        case FrameDirection::kClientToServer:
            return tag == FrameTag::kExecStream || tag == FrameTag::kPrepareSet
                   || tag == FrameTag::kExecBatch;
        case FrameDirection::kServerToClient:
            return tag == FrameTag::kMeta || tag == FrameTag::kRow || tag == FrameTag::kCommandOk
                   || tag == FrameTag::kResultEnd || tag == FrameTag::kTransactionAbort
                   || tag == FrameTag::kError || tag == FrameTag::kPrepareOk
                   || tag == FrameTag::kBatchResult;
    }
    return false;
}

auto flags_are_valid(const FrameTag tag, const std::uint8_t flags) noexcept -> bool {
    if (tag == FrameTag::kExecBatch) {
        return flags == kRmdbAutoAbortFrameFlag;
    }
    return flags == kRmdbNoFrameFlags;
}

auto inbound_error_code(const FrameDirection direction) noexcept -> base::ErrorCode {
    return direction == FrameDirection::kClientToServer ? base::ErrorCode::kInvalidArgument
                                                        : base::ErrorCode::kCorruption;
}

auto resize_reusable_buffer(
    std::vector<std::byte>& buffer,
    const std::size_t size,
    const std::string_view operation
) -> base::Status {
    try {
        buffer.resize(size);
    } catch (const std::bad_alloc&) {
        return base::fail(protocol_error(
            base::ErrorCode::kResourceExhausted,
            "RMDB frame buffer allocation failed",
            operation
        ));
    } catch (const std::length_error&) {
        return base::fail(protocol_error(
            base::ErrorCode::kResourceExhausted,
            "RMDB frame buffer length is unsupported",
            operation
        ));
    }
    return {};
}

auto payload_starts_in_buffer(
    const base::Bytes payload,
    const std::vector<std::byte>& buffer
) noexcept -> bool {
    if (payload.empty() || buffer.empty()) {
        return false;
    }
    const std::less<> pointer_less;
    const std::byte* const payload_start = payload.data();
    const std::byte* const buffer_start = buffer.data();
    // std::less provides a total order even for pointers to separate objects.
    const base::Bytes buffer_view{buffer.data(), buffer.size()};
    const std::byte* const buffer_end = std::to_address(buffer_view.end());
    return !pointer_less(payload_start, buffer_start) && pointer_less(payload_start, buffer_end);
}

auto validate_frame_header(
    const FrameDirection direction,
    const FrameHeader& header,
    const base::ErrorCode code,
    const std::string_view operation
) -> base::Result<ValidatedFrameHeader> {
    if (header.payload_length > kMaximumRmdbFramePayloadSize) {
        return base::fail(
            protocol_error(code, "RMDB frame payload exceeds the 1 MiB limit", operation)
        );
    }
    if (header.reserved != 0U) {
        return base::fail(
            protocol_error(code, "RMDB frame used non-zero reserved bytes", operation)
        );
    }
    const auto tag = known_frame_tag(header.tag);
    if (!tag.has_value()) {
        return base::fail(protocol_error(code, "RMDB frame used an unknown tag", operation));
    }
    if (!tag_allows_direction(direction, *tag)) {
        return base::fail(
            protocol_error(code, "RMDB frame tag has the wrong direction", operation)
        );
    }
    if (!flags_are_valid(*tag, header.flags)) {
        return base::fail(protocol_error(code, "RMDB frame used unsupported flags", operation));
    }
    return ValidatedFrameHeader{
        .payload_length = header.payload_length,
        .tag = *tag,
        .flags = header.flags,
    };
}

}  // namespace

auto validate_rmdb_v3_preface(const base::Bytes bytes) -> base::Status {
    if (bytes.size() != kRmdbV3PrefaceSize) {
        return base::fail(handshake_error("RMDB preface has the wrong length"));
    }
    const base::Bytes expected{kRmdbV3Preface.data(), kRmdbV3Preface.size()};
    if (!std::equal(bytes.begin(), bytes.end(), expected.begin(), expected.end())) {
        return base::fail(handshake_error("RMDB preface version is unsupported"));
    }
    return {};
}

auto validate_inbound_frame_header(const FrameDirection direction, const FrameHeader& header)
    -> base::Result<ValidatedFrameHeader> {
    return validate_frame_header(
        direction,
        header,
        inbound_error_code(direction),
        direction == FrameDirection::kClientToServer ? "validate request header"
                                                     : "validate response header"
    );
}

auto validate_inbound_frame_envelope(const FrameDirection direction, const FrameView& frame)
    -> base::Result<ValidatedFrameHeader> {
    if (frame.payload.size() != static_cast<std::size_t>(frame.header.payload_length)) {
        return base::fail(protocol_error(
            inbound_error_code(direction),
            "RMDB frame payload length did not match its header",
            direction == FrameDirection::kClientToServer ? "validate request envelope"
                                                         : "validate response envelope"
        ));
    }
    return validate_inbound_frame_header(direction, frame.header);
}

FrameDecoder::FrameDecoder(FrameDecoder&& other) noexcept
    : header_bytes_{
          std::exchange(other.header_bytes_, std::array<std::byte, kRmdbFrameHeaderSize>{})
      },
      header_bytes_received_{std::exchange(other.header_bytes_received_, 0U)},
      header_{std::exchange(other.header_, FrameHeader{})},
      payload_{std::move(other.payload_)},
      payload_bytes_received_{std::exchange(other.payload_bytes_received_, 0U)},
      frame_ready_{std::exchange(other.frame_ready_, false)} {
    other.payload_.clear();
}

auto FrameDecoder::operator=(FrameDecoder&& other) noexcept -> FrameDecoder& {
    if (this != &other) {
        header_bytes_ =
            std::exchange(other.header_bytes_, std::array<std::byte, kRmdbFrameHeaderSize>{});
        header_bytes_received_ = std::exchange(other.header_bytes_received_, 0U);
        header_ = std::exchange(other.header_, FrameHeader{});
        payload_ = std::move(other.payload_);
        other.payload_.clear();
        payload_bytes_received_ = std::exchange(other.payload_bytes_received_, 0U);
        frame_ready_ = std::exchange(other.frame_ready_, false);
    }
    return *this;
}

auto FrameDecoder::consume(const base::Bytes input) -> base::Result<FrameDecodeStep> {
    if (frame_ready_) {
        reset_current_frame();
    }

    FrameDecodeStep step{};
    auto header = consume_header(input);
    if (!header) {
        return base::fail(std::move(header).error());
    }
    step.consumed = header.value();
    if (header_bytes_received_ == kRmdbFrameHeaderSize && !frame_ready_) {
        auto payload = consume_payload(input.subspan(step.consumed));
        if (!payload) {
            return base::fail(std::move(payload).error());
        }
        step.consumed += payload.value();
    }

    if (frame_ready_) {
        step.frame = FrameView{
            .header = header_,
            .payload = base::Bytes{payload_.data(), payload_.size()},
        };
    }
    return step;
}

void FrameDecoder::reset() noexcept {
    reset_current_frame();
}

auto FrameDecoder::has_incomplete_frame() const noexcept -> bool {
    return header_bytes_received_ != 0U && !frame_ready_;
}

auto FrameDecoder::payload_capacity() const noexcept -> std::size_t {
    return payload_.capacity();
}

auto FrameDecoder::consume_header(const base::Bytes input) -> base::Result<std::size_t> {
    if (header_bytes_received_ == kRmdbFrameHeaderSize) {
        return 0U;
    }

    const std::size_t header_remaining = kRmdbFrameHeaderSize - header_bytes_received_;
    const std::size_t copied = std::min(input.size(), header_remaining);
    const auto source = input.first(copied);
    const auto target = std::span<std::byte>{header_bytes_}.subspan(header_bytes_received_, copied);
    std::copy(source.begin(), source.end(), target.begin());
    header_bytes_received_ += copied;
    if (header_bytes_received_ != kRmdbFrameHeaderSize) {
        return copied;
    }

    auto status = begin_payload();
    if (!status) {
        return base::fail(std::move(status).error());
    }
    return copied;
}

auto FrameDecoder::consume_payload(const base::Bytes input) -> base::Result<std::size_t> {
    if (frame_ready_) {
        return 0U;
    }

    const std::size_t payload_remaining = payload_.size() - payload_bytes_received_;
    const std::size_t copied = std::min(input.size(), payload_remaining);
    const auto source = input.first(copied);
    const auto target = std::span<std::byte>{payload_}.subspan(payload_bytes_received_, copied);
    std::copy(source.begin(), source.end(), target.begin());
    payload_bytes_received_ += copied;
    frame_ready_ = payload_bytes_received_ == payload_.size();
    return copied;
}

auto FrameDecoder::begin_payload() -> base::Status {
    base::BigEndianReader reader{
        base::Bytes{header_bytes_.data(), header_bytes_.size()},
    };
    auto payload_length = reader.read_u32();
    auto tag = reader.read_u8();
    auto flags = reader.read_u8();
    auto reserved = reader.read_u16();
    if (!payload_length || !tag || !flags || !reserved || !reader.finish()) {
        reset_current_frame();
        return base::fail(protocol_error(
            base::ErrorCode::kInvariantViolation,
            "RMDB fixed frame header could not be decoded",
            "decode frame header"
        ));
    }
    if (payload_length.value() > kMaximumRmdbFramePayloadSize) {
        reset_current_frame();
        return base::fail(protocol_error(
            base::ErrorCode::kCorruption,
            "RMDB frame payload exceeds the 1 MiB limit",
            "decode frame header"
        ));
    }

    header_ = FrameHeader{
        .payload_length = payload_length.value(),
        .tag = tag.value(),
        .flags = flags.value(),
        .reserved = reserved.value(),
    };
    auto resized = resize_reusable_buffer(
        payload_,
        static_cast<std::size_t>(header_.payload_length),
        "allocate decoded frame payload"
    );
    if (!resized) {
        reset_current_frame();
        return base::fail(std::move(resized).error());
    }
    payload_bytes_received_ = 0U;
    frame_ready_ = payload_.empty();
    return {};
}

void FrameDecoder::reset_current_frame() noexcept {
    header_bytes_received_ = 0U;
    header_ = FrameHeader{};
    payload_.clear();
    payload_bytes_received_ = 0U;
    frame_ready_ = false;
}

auto FrameEncoder::encode(
    const FrameDirection direction,
    const FrameTag tag,
    const std::uint8_t flags,
    const base::Bytes payload
) -> base::Result<base::Bytes> {
    if (payload.size() > kMaximumRmdbFramePayloadSize) {
        return base::fail(protocol_error(
            base::ErrorCode::kOutOfRange,
            "RMDB outbound payload exceeds the 1 MiB limit",
            "encode frame"
        ));
    }
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(protocol_error(
            base::ErrorCode::kOutOfRange,
            "RMDB outbound payload exceeds u32 length",
            "encode frame"
        ));
    }
    const FrameHeader header{
        .payload_length = static_cast<std::uint32_t>(payload.size()),
        .tag = static_cast<std::uint8_t>(tag),
        .flags = flags,
        .reserved = 0U,
    };
    auto validated = validate_frame_header(
        direction,
        header,
        base::ErrorCode::kInvalidArgument,
        "validate outbound frame"
    );
    if (!validated) {
        return base::fail(std::move(validated).error());
    }
    if (payload.size() > std::numeric_limits<std::size_t>::max() - kRmdbFrameHeaderSize) {
        return base::fail(protocol_error(
            base::ErrorCode::kOutOfRange,
            "RMDB encoded frame length overflowed",
            "encode frame"
        ));
    }

    auto stable_payload = stage_aliased_payload(payload);
    if (!stable_payload) {
        return base::fail(std::move(stable_payload).error());
    }

    auto resized = resize_reusable_buffer(
        bytes_,
        kRmdbFrameHeaderSize + payload.size(),
        "allocate encoded frame"
    );
    if (!resized) {
        return base::fail(std::move(resized).error());
    }
    base::BigEndianWriter writer{base::MutableBytes{bytes_.data(), kRmdbFrameHeaderSize}};
    auto status = writer.write_u32(header.payload_length);
    if (status) {
        status = writer.write_u8(header.tag);
    }
    if (status) {
        status = writer.write_u8(header.flags);
    }
    if (status) {
        status = writer.write_u16(header.reserved);
    }
    if (!status || writer.position() != kRmdbFrameHeaderSize) {
        return base::fail(protocol_error(
            base::ErrorCode::kInvariantViolation,
            "RMDB frame encoder did not write its fixed header",
            "encode frame"
        ));
    }
    const auto output_payload =
        std::span<std::byte>{bytes_}.subspan(kRmdbFrameHeaderSize, stable_payload.value().size());
    std::copy(stable_payload.value().begin(), stable_payload.value().end(), output_payload.begin());
    return base::Bytes{bytes_.data(), bytes_.size()};
}

void FrameEncoder::reset() noexcept {
    bytes_.clear();
    staged_payload_.clear();
}

auto FrameEncoder::buffer_capacity() const noexcept -> std::size_t {
    return bytes_.capacity();
}

auto FrameEncoder::stage_aliased_payload(const base::Bytes payload) -> base::Result<base::Bytes> {
    if (!payload_starts_in_buffer(payload, bytes_)) {
        return payload;
    }
    auto resized =
        resize_reusable_buffer(staged_payload_, payload.size(), "stage aliased outbound payload");
    if (!resized) {
        return base::fail(std::move(resized).error());
    }
    std::copy(payload.begin(), payload.end(), staged_payload_.begin());
    return base::Bytes{staged_payload_.data(), staged_payload_.size()};
}

}  // namespace db::protocol
