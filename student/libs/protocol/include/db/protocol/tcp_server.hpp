#pragma once

#include <chrono>
#include <cstddef>
#include <memory>

#include "db/base/result.hpp"
#include "db/io/socket.hpp"
#include "db/protocol/session_port.hpp"

namespace db::protocol {

struct TcpServerOptions final {
    std::size_t maximum_concurrent_connections{32U};
    std::size_t listen_backlog{128U};
    // One absolute deadline covers the exact 8-byte RMDB preface. A peer that
    // never finishes it does not consume a worker indefinitely.
    std::chrono::milliseconds handshake_timeout{5000};
    // At a frame boundary, this is the maximum quiet interval before a client
    // is considered disconnected or stalled.
    std::chrono::milliseconds idle_timeout{30000};
    // Once a frame starts, this absolute deadline is not extended by trickle
    // bytes, preventing a slowloris peer from holding a partial frame forever.
    std::chrono::milliseconds request_timeout{5000};
    // A complete outgoing frame must make progress before this deadline. The
    // response encoder has no unbounded outbound queue, so this is the
    // connection's backpressure bound.
    std::chrono::milliseconds response_write_timeout{5000};
    // Maximum control-plane observation latency while an accept/read/write
    // readiness wait is otherwise idle.
    std::chrono::milliseconds shutdown_poll_interval{25};
};

// Blocking RMDB v3 connection runtime. One caller owns the accept loop at a
// time. serve_n() never creates more than maximum_concurrent_connections live
// worker threads and joins every worker before it returns. Absolute
// handshake/request deadlines, bounded response writes, peer-abort cleanup,
// and cooperative shutdown are part of the runtime contract.
class TcpServer final {
public:
    static auto bind(
        const io::TcpEndpoint& endpoint,
        SessionFactoryPort& session_factory,
        TcpServerOptions options = {}
    ) -> base::Result<TcpServer>;

    TcpServer(const TcpServer&) = delete;
    auto operator=(const TcpServer&) -> TcpServer& = delete;
    TcpServer(TcpServer&& other) noexcept;
    auto operator=(TcpServer&& other) noexcept -> TcpServer&;
    ~TcpServer();

    auto local_endpoint() const -> base::Result<io::TcpEndpoint>;

    // Serve one accepted connection synchronously. A peer's bad/truncated
    // preface is returned to the direct caller after the socket is closed.
    auto serve_one() -> base::Status;

    // Serve exactly connection_count accepted connections. Peer I/O and wire
    // failures are isolated to their own worker; a session-factory/cleanup or
    // runtime failure is returned after all accepted workers have joined.
    auto serve_n(std::size_t connection_count) -> base::Status;

    // Serves connections until request_shutdown() is called or a local runtime
    // failure occurs. Completed workers are reaped before a slot is reused, so
    // this long-lived mode retains at most the configured worker-handle count.
    // A requested shutdown closes the listener only after every worker has
    // observed it and cleaned its session.
    auto serve_forever() -> base::Status;

    // Thread-safe and idempotent. It causes each accepted connection to leave
    // its bounded readiness wait within shutdown_poll_interval. A connection
    // that observes the request before opening a session does not open one;
    // it does not close a socket from a foreign thread.
    void request_shutdown() noexcept;
    auto shutdown_requested() const noexcept -> bool;

private:
    struct Control;
    TcpServer(
        io::TcpListener listener,
        SessionFactoryPort& session_factory,
        TcpServerOptions options,
        std::unique_ptr<Control> control
    );

    struct ConnectionOutcome;

    auto serve_connection(io::TcpStream stream) -> ConnectionOutcome;

    io::TcpListener listener_;
    SessionFactoryPort* session_factory_{nullptr};
    TcpServerOptions options_{};
    std::unique_ptr<Control> control_;
};

}  // namespace db::protocol
