#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"

namespace db::io {

// Result of one bounded readiness wait. A timeout is ordinary control flow,
// not an I/O error; callers choose the deadline policy appropriate to their
// protocol or application.
enum class TcpWaitResult : std::uint8_t {
    kReady,
    kTimedOut,
};

// An IPv4 TCP endpoint. The stored octets are in wire order; port zero is
// valid only for listener binding and asks the kernel to choose a free port.
struct TcpEndpoint final {
    std::array<std::uint8_t, 4> address{127U, 0U, 0U, 1U};
    std::uint16_t port{0};

    static auto loopback(std::uint16_t port = 0U) noexcept -> TcpEndpoint;
    static auto parse(std::string_view text) -> base::Result<TcpEndpoint>;
    auto to_string() const -> std::string;

    auto operator==(const TcpEndpoint&) const noexcept -> bool = default;
};

// Move-only blocking TCP stream. Reads return zero only for a clean peer EOF;
// read_exact() turns a premature EOF into a structured I/O error. All writes
// suppress SIGPIPE and retry interrupted syscalls.
class TcpStream final {
public:
    static auto connect(const TcpEndpoint& endpoint) -> base::Result<TcpStream>;

    ~TcpStream() noexcept;

    TcpStream(const TcpStream&) = delete;
    auto operator=(const TcpStream&) -> TcpStream& = delete;
    TcpStream(TcpStream&& other) noexcept;
    auto operator=(TcpStream&& other) noexcept -> TcpStream&;

    auto is_open() const noexcept -> bool;
    auto native_handle() const noexcept -> int;

    auto read_some(base::MutableBytes output) const -> base::Result<std::size_t>;
    // Performs one nonblocking receive without changing the descriptor's
    // blocking mode. If no byte is currently available it returns
    // kUnavailable, so a caller can wait again without a hidden block.
    auto try_read_some(base::MutableBytes output) const -> base::Result<std::size_t>;
    auto read_exact(base::MutableBytes output) const -> base::Status;
    auto write_some(base::Bytes input) const -> base::Result<std::size_t>;
    // Performs one nonblocking send without changing the descriptor's
    // blocking mode. If the peer's receive window cannot accept bytes it
    // returns kUnavailable rather than waiting indefinitely.
    auto try_write_some(base::Bytes input) const -> base::Result<std::size_t>;
    auto write_all(base::Bytes input) const -> base::Status;
    auto wait_readable(std::chrono::milliseconds timeout) const -> base::Result<TcpWaitResult>;
    auto wait_writable(std::chrono::milliseconds timeout) const -> base::Result<TcpWaitResult>;
    auto set_no_delay(bool enabled = true) const -> base::Status;
    auto close() -> base::Status;

private:
    explicit TcpStream(int descriptor) noexcept;
    void close_best_effort() noexcept;

    int descriptor_{-1};

    friend class TcpListener;
};

// Move-only IPv4 listener. Its descriptor is internally nonblocking so a
// one-shot accept cannot hide a blocking syscall; accept() retains the public
// blocking facade and every returned stream is blocking and close-on-exec.
class TcpListener final {
public:
    static auto bind(const TcpEndpoint& endpoint, std::size_t backlog = 128U)
        -> base::Result<TcpListener>;

    ~TcpListener() noexcept;

    TcpListener(const TcpListener&) = delete;
    auto operator=(const TcpListener&) -> TcpListener& = delete;
    TcpListener(TcpListener&& other) noexcept;
    auto operator=(TcpListener&& other) noexcept -> TcpListener&;

    auto is_open() const noexcept -> bool;
    auto native_handle() const noexcept -> int;
    auto local_endpoint() const -> base::Result<TcpEndpoint>;
    auto wait_readable(std::chrono::milliseconds timeout) const -> base::Result<TcpWaitResult>;
    // Performs one nonblocking accept without changing the listener's mode.
    // The returned stream is restored to the normal blocking ownership model.
    // An empty accept queue returns kUnavailable so a caller can wait again.
    auto try_accept() const -> base::Result<TcpStream>;
    auto accept() const -> base::Result<TcpStream>;
    auto close() -> base::Status;

private:
    explicit TcpListener(int descriptor) noexcept;
    void close_best_effort() noexcept;

    int descriptor_{-1};
};

}  // namespace db::io
