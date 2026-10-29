#pragma once

#include <cstddef>
#include <memory>

#include "db/base/bytes.hpp"
#include "db/base/error.hpp"
#include "db/base/result.hpp"
#include "db/protocol/request_dispatcher.hpp"

namespace db::protocol {

// Consumer-owned output port for one already framed server response. The view
// is owned by ResponseEncoder and stays valid only until this call returns or
// the encoder emits another frame, so implementations must consume it
// synchronously. Transport adapters map a failed write to a structured error;
// this port deliberately knows nothing about sockets.
class ResponseSink {
public:
    virtual ~ResponseSink() = default;

    ResponseSink(const ResponseSink&) = delete;
    auto operator=(const ResponseSink&) -> ResponseSink& = delete;
    ResponseSink(ResponseSink&&) = delete;
    auto operator=(ResponseSink&&) -> ResponseSink& = delete;

    virtual auto write_encoded_frame(base::Bytes frame) -> base::Status = 0;

protected:
    ResponseSink() = default;
};

// Serializes one validated request result into its RMDB v3 response sequence.
// It owns reusable frame and payload buffers, but neither a socket nor a
// session. Every output frame is validated by FrameEncoder before it is
// synchronously handed to ResponseSink.
class ResponseEncoder final {
public:
    // Incomplete solely to keep reusable buffers out of the public ABI.
    struct Impl;

    ResponseEncoder();

    ResponseEncoder(const ResponseEncoder&) = delete;
    auto operator=(const ResponseEncoder&) -> ResponseEncoder& = delete;
    ResponseEncoder(ResponseEncoder&& other) noexcept;
    auto operator=(ResponseEncoder&& other) noexcept -> ResponseEncoder&;
    ~ResponseEncoder();

    // Emits COMMAND_OK, META/ROW/RESULT_END, PREPARE_OK, BATCH_RESULT, ERROR,
    // or TRANSACTION_ABORT according to response. A malformed semantic model
    // is a structured invariant error and is never serialized partially before
    // its own offending frame.
    auto emit(const RequestResponse& response, ResponseSink& sink) -> base::Status;

    // Lets the connection runtime report an encoder/model failure with the
    // same bounded UTF-8 terminal encoding. It is intentionally separate from
    // emit() so a transport owner can decide whether an I/O failure is still
    // writable before attempting this fallback.
    auto
    emit_terminal_error(TerminalResponseKind kind, const base::Error& error, ResponseSink& sink)
        -> base::Status;

    auto frame_buffer_capacity() const noexcept -> std::size_t;
    auto payload_buffer_capacity() const noexcept -> std::size_t;

private:
    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::protocol
