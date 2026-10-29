#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"

namespace db::protocol {

inline constexpr std::size_t kRmdbV3PrefaceSize = 8U;
inline constexpr std::size_t kRmdbFrameHeaderSize = 8U;
inline constexpr std::size_t kMaximumRmdbFramePayloadSize =
    std::size_t{1U} * std::size_t{1024U} * std::size_t{1024U};
inline constexpr std::size_t kMaximumRmdbDiagnosticSize = std::size_t{64U} * std::size_t{1024U};
inline constexpr std::size_t kMaximumRmdbPreparedStatements = 256U;
inline constexpr std::size_t kMaximumRmdbBatchOperations = 256U;

inline constexpr std::uint8_t kRmdbNoFrameFlags = 0U;
inline constexpr std::uint8_t kRmdbAutoAbortFrameFlag = 0x01U;

inline constexpr std::array<std::byte, kRmdbV3PrefaceSize> kRmdbV3Preface{
    std::byte{0x52U},
    std::byte{0x4DU},
    std::byte{0x44U},
    std::byte{0x42U},
    std::byte{0x00U},
    std::byte{0x03U},
    std::byte{0x00U},
    std::byte{0x00U},
};

enum class FrameDirection : std::uint8_t {
    kClientToServer,
    kServerToClient,
};

enum class FrameTag : std::uint8_t {
    kMeta = 0x01U,
    kRow = 0x02U,
    kCommandOk = 0x10U,
    kResultEnd = 0x11U,
    kTransactionAbort = 0x12U,
    kError = 0x13U,
    kPrepareOk = 0x14U,
    kBatchResult = 0x15U,
    kExecStream = 0x20U,
    kPrepareSet = 0x21U,
    kExecBatch = 0x22U,
};

struct FrameHeader final {
    std::uint32_t payload_length{0};
    std::uint8_t tag{0};
    std::uint8_t flags{kRmdbNoFrameFlags};
    std::uint16_t reserved{0};

    auto operator==(const FrameHeader&) const noexcept -> bool = default;
};

struct ValidatedFrameHeader final {
    std::uint32_t payload_length{0};
    FrameTag tag{FrameTag::kMeta};
    std::uint8_t flags{kRmdbNoFrameFlags};

    auto operator==(const ValidatedFrameHeader&) const noexcept -> bool = default;
};

// A view of one complete frame. The payload is owned by FrameDecoder and stays
// valid only until that decoder's next consume(), reset(), move, or destruction.
struct FrameView final {
    FrameHeader header;
    base::Bytes payload;
};

struct FrameDecodeStep final {
    std::size_t consumed{0};
    std::optional<FrameView> frame;
};

// Check an exact RMDB v3 handshake. A peer preface mismatch is wire
// corruption, never an SQL error.
auto validate_rmdb_v3_preface(base::Bytes bytes) -> base::Status;

// Validate an inbound header after framing has completed. Invalid client
// request headers map to kInvalidArgument so a server can emit ERROR and keep
// the connection; invalid server response headers map to kCorruption so a
// client rejects the peer response.
auto validate_inbound_frame_header(FrameDirection direction, const FrameHeader& header)
    -> base::Result<ValidatedFrameHeader>;

// Validate a complete inbound frame envelope. In addition to the header
// rules, this rejects an externally assembled FrameView whose payload size
// does not match the declared length. Payload-specific semantics belong to
// later message codecs.
auto validate_inbound_frame_envelope(FrameDirection direction, const FrameView& frame)
    -> base::Result<ValidatedFrameHeader>;

// Incrementally decode one bounded frame from caller-owned chunks. A step
// stops at the first complete frame and reports how many bytes it consumed;
// callers feed any suffix into the next call after processing the returned
// FrameView. The internal payload allocation is retained for later frames.
class FrameDecoder final {
public:
    FrameDecoder() = default;

    FrameDecoder(const FrameDecoder&) = delete;
    auto operator=(const FrameDecoder&) -> FrameDecoder& = delete;
    // Moving transfers any partial header/payload and invalidates an
    // outstanding FrameView. The source becomes an empty reusable decoder.
    FrameDecoder(FrameDecoder&& other) noexcept;
    auto operator=(FrameDecoder&& other) noexcept -> FrameDecoder&;
    ~FrameDecoder() = default;

    auto consume(base::Bytes input) -> base::Result<FrameDecodeStep>;

    // Reset framing state while retaining any payload allocation for reuse.
    void reset() noexcept;

    // Reports whether a header or payload has been partially consumed. A
    // connection runtime uses this to distinguish clean EOF at a frame
    // boundary from truncated framing without inspecting codec internals.
    auto has_incomplete_frame() const noexcept -> bool;

    auto payload_capacity() const noexcept -> std::size_t;

private:
    auto consume_header(base::Bytes input) -> base::Result<std::size_t>;
    auto consume_payload(base::Bytes input) -> base::Result<std::size_t>;
    auto begin_payload() -> base::Status;
    void reset_current_frame() noexcept;

    std::array<std::byte, kRmdbFrameHeaderSize> header_bytes_{};
    std::size_t header_bytes_received_{0};
    FrameHeader header_{};
    std::vector<std::byte> payload_;
    std::size_t payload_bytes_received_{0};
    bool frame_ready_{false};
};

// Reuse one contiguous header-plus-payload allocation when encoding outbound
// frames. The returned view is invalidated by the next encode(), reset(), move,
// or destruction of this encoder. Passing that previous view back as payload is
// supported: the encoder stages it before resizing its output storage.
class FrameEncoder final {
public:
    FrameEncoder() = default;

    FrameEncoder(const FrameEncoder&) = delete;
    auto operator=(const FrameEncoder&) -> FrameEncoder& = delete;
    FrameEncoder(FrameEncoder&&) noexcept = default;
    auto operator=(FrameEncoder&&) noexcept -> FrameEncoder& = default;
    ~FrameEncoder() = default;

    auto encode(FrameDirection direction, FrameTag tag, std::uint8_t flags, base::Bytes payload)
        -> base::Result<base::Bytes>;

    // Clear the logical output while retaining allocation capacity for reuse.
    void reset() noexcept;

    auto buffer_capacity() const noexcept -> std::size_t;

private:
    auto stage_aliased_payload(base::Bytes payload) -> base::Result<base::Bytes>;

    std::vector<std::byte> bytes_;
    std::vector<std::byte> staged_payload_;
};

}  // namespace db::protocol
