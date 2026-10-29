#include "db/io/socket.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "db/base/error.hpp"

#include "unique_file_descriptor.hpp"

namespace db::io {
namespace {

auto map_socket_errno(const int error_number) noexcept -> base::ErrorCode {
    switch (error_number) {
        case EADDRINUSE:
            return base::ErrorCode::kAlreadyExists;
        case EINVAL:
        case EAFNOSUPPORT:
        case EDESTADDRREQ:
        case ENOTSOCK:
            return base::ErrorCode::kInvalidArgument;
        case EMFILE:
        case ENFILE:
        case ENOBUFS:
        case ENOMEM:
            return base::ErrorCode::kResourceExhausted;
        default:
            return base::ErrorCode::kIo;
    }
}

auto socket_error(const int error_number, const std::string_view operation) -> base::Error {
    return base::Error{
        map_socket_errno(error_number),
        std::string{operation}.append(" failed"),
        std::error_code{error_number, std::generic_category()},
    }
        .with_context("io.tcp", std::string{operation});
}

auto closed_error(const std::string_view operation) -> base::Error {
    return base::Error{
        base::ErrorCode::kUnavailable,
        "TCP socket is closed",
    }
        .with_context("io.tcp", std::string{operation});
}

auto io_error(std::string message, const std::string_view operation) -> base::Error {
    return base::Error{base::ErrorCode::kIo, std::move(message)}.with_context(
        "io.tcp",
        std::string{operation}
    );
}

auto would_block_error(const int error_number, const std::string_view operation) -> base::Error {
    return base::Error{
        base::ErrorCode::kUnavailable,
        "TCP socket is not ready for the requested nonblocking operation",
        std::error_code{error_number, std::generic_category()},
    }
        .with_context("io.tcp", std::string{operation});
}

auto as_sockaddr(const TcpEndpoint& endpoint) noexcept -> sockaddr_in {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    std::memcpy(&address.sin_addr, endpoint.address.data(), endpoint.address.size());
    return address;
}

auto as_socket_address(const sockaddr_in& address) noexcept -> const sockaddr* {
    // POSIX requires a sockaddr view for an address-family-specific object.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<const sockaddr*>(&address);
}

auto as_socket_address(sockaddr_in& address) noexcept -> sockaddr* {
    // POSIX writes a sockaddr into caller-owned suitably aligned storage.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<sockaddr*>(&address);
}

auto endpoint_from_sockaddr(const sockaddr_in& address, const socklen_t length)
    -> base::Result<TcpEndpoint> {
    if (address.sin_family != AF_INET || length < static_cast<socklen_t>(sizeof(sockaddr_in))) {
        return base::fail(
            base::Error{
                base::ErrorCode::kCorruption,
                "TCP socket reported a non-IPv4 endpoint",
            }
                .with_context("io.tcp", "decode endpoint")
        );
    }
    TcpEndpoint endpoint{};
    std::memcpy(endpoint.address.data(), &address.sin_addr, endpoint.address.size());
    endpoint.port = ntohs(address.sin_port);
    return endpoint;
}

auto set_close_on_exec(const int descriptor, const std::string_view operation) -> base::Status {
    int flags = -1;
    do {
        // POSIX exposes fcntl(2) as a variadic C API.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        flags = ::fcntl(descriptor, F_GETFD);
    } while (flags < 0 && errno == EINTR);
    if (flags < 0) {
        return base::fail(socket_error(errno, operation));
    }

    int result = -1;
    do {
        // POSIX exposes fcntl(2) as a variadic C API.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        result = ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(socket_error(errno, operation));
    }
    return {};
}

auto set_blocking_mode(const int descriptor, const bool blocking, const std::string_view operation)
    -> base::Status {
    int flags = -1;
    do {
        // POSIX exposes fcntl(2) as a variadic C API.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        flags = ::fcntl(descriptor, F_GETFL);
    } while (flags < 0 && errno == EINTR);
    if (flags < 0) {
        return base::fail(socket_error(errno, operation));
    }

    const int updated_flags = blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK;
    int result = -1;
    do {
        // POSIX exposes fcntl(2) as a variadic C API.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        result = ::fcntl(descriptor, F_SETFL, updated_flags);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(socket_error(errno, operation));
    }
    return {};
}

auto bounded_io_size(const std::size_t requested) noexcept -> std::size_t {
    return std::min(requested, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
}

constexpr auto would_block_errno(const int error_number) noexcept -> bool {
    // Linux exposes EAGAIN and EWOULDBLOCK as the same value for sockets.
    return error_number == EAGAIN;
}

auto bounded_poll_timeout_milliseconds(const std::chrono::steady_clock::duration remaining) noexcept
    -> int {
    const auto rounded = std::chrono::ceil<std::chrono::milliseconds>(remaining);
    if (rounded.count() > static_cast<std::int64_t>(INT_MAX)) {
        return INT_MAX;
    }
    return static_cast<int>(rounded.count());
}

auto wait_for_socket_events(
    const int descriptor,
    const short requested_events,
    const std::chrono::milliseconds timeout,
    const std::string_view operation
) -> base::Result<TcpWaitResult> {
    if (timeout.count() < 0) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "TCP readiness timeout must not be negative",
            }
                .with_context("io.tcp", std::string{operation})
        );
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;

    pollfd descriptor_events{
        .fd = descriptor,
        .events = requested_events,
        .revents = 0,
    };
    bool allow_immediate_poll = timeout.count() == 0;
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline && !allow_immediate_poll) {
            return TcpWaitResult::kTimedOut;
        }
        const int current_wait_milliseconds =
            allow_immediate_poll ? 0 : bounded_poll_timeout_milliseconds(deadline - now);
        allow_immediate_poll = false;

        descriptor_events.revents = 0;
        const int result = ::poll(&descriptor_events, 1U, current_wait_milliseconds);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return base::fail(socket_error(errno, operation));
        }
        if (result == 0) {
            return TcpWaitResult::kTimedOut;
        }
        if ((descriptor_events.revents & POLLNVAL) != 0) {
            return base::fail(socket_error(EBADF, operation));
        }
        if ((descriptor_events.revents & POLLERR) != 0) {
            int pending_error = 0;
            socklen_t pending_error_size = sizeof(pending_error);
            if (::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &pending_error, &pending_error_size)
                != 0) {
                return base::fail(socket_error(errno, operation));
            }
            if (pending_error != 0) {
                return base::fail(socket_error(pending_error, operation));
            }
            return base::fail(io_error("TCP socket reported an asynchronous error", operation));
        }
        return TcpWaitResult::kReady;
    }
}

auto receive_some(
    const int descriptor,
    const base::MutableBytes output,
    const int flags,
    const bool report_would_block
) -> base::Result<std::size_t> {
    ssize_t result = -1;
    do {
        result = ::recv(descriptor, output.data(), bounded_io_size(output.size()), flags);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        if (report_would_block && would_block_errno(errno)) {
            return base::fail(would_block_error(errno, "read TCP stream"));
        }
        return base::fail(socket_error(errno, "read TCP stream"));
    }
    return static_cast<std::size_t>(result);
}

auto send_some(
    const int descriptor,
    const base::Bytes input,
    const int flags,
    const bool report_would_block
) -> base::Result<std::size_t> {
    ssize_t result = -1;
    do {
        result = ::send(descriptor, input.data(), bounded_io_size(input.size()), flags);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        if (report_would_block && would_block_errno(errno)) {
            return base::fail(would_block_error(errno, "write TCP stream"));
        }
        return base::fail(socket_error(errno, "write TCP stream"));
    }
    return static_cast<std::size_t>(result);
}

}  // namespace

auto TcpEndpoint::loopback(const std::uint16_t port) noexcept -> TcpEndpoint {
    return TcpEndpoint{.address = {127U, 0U, 0U, 1U}, .port = port};
}

auto TcpEndpoint::parse(const std::string_view text) -> base::Result<TcpEndpoint> {
    const std::size_t separator = text.rfind(':');
    if (separator == std::string_view::npos || separator == 0U || separator + 1U == text.size()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "TCP endpoint must be an IPv4 address followed by a port",
            }
                .with_context("io.tcp", "parse endpoint")
        );
    }

    unsigned int port = 0U;
    const std::string_view port_text = text.substr(separator + 1U);
    const auto parsed_port = std::from_chars(port_text.begin(), port_text.end(), port);
    if (parsed_port.ec != std::errc{} || parsed_port.ptr != port_text.end()
        || port > std::numeric_limits<std::uint16_t>::max()) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "TCP endpoint port must be a decimal u16",
            }
                .with_context("io.tcp", "parse endpoint")
        );
    }

    const std::string address_text{text.substr(0U, separator)};
    in_addr parsed_address{};
    if (::inet_pton(AF_INET, address_text.c_str(), &parsed_address) != 1) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "TCP endpoint must contain a numeric IPv4 address",
            }
                .with_context("io.tcp", "parse endpoint")
        );
    }

    TcpEndpoint endpoint{};
    std::memcpy(endpoint.address.data(), &parsed_address, endpoint.address.size());
    endpoint.port = static_cast<std::uint16_t>(port);
    return endpoint;
}

auto TcpEndpoint::to_string() const -> std::string {
    return std::to_string(address[0]) + "." + std::to_string(address[1]) + "."
           + std::to_string(address[2]) + "." + std::to_string(address[3]) + ":"
           + std::to_string(port);
}

auto TcpStream::connect(const TcpEndpoint& endpoint) -> base::Result<TcpStream> {
    int descriptor = -1;
    do {
        descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        return base::fail(socket_error(errno, "create TCP stream"));
    }
    internal::UniqueFileDescriptor acquired{descriptor};

    auto close_on_exec = set_close_on_exec(acquired.get(), "set TCP stream close-on-exec");
    if (!close_on_exec) {
        return base::fail(std::move(close_on_exec).error());
    }

    const sockaddr_in address = as_sockaddr(endpoint);
    int result = -1;
    do {
        result = ::connect(
            acquired.get(),
            as_socket_address(address),
            static_cast<socklen_t>(sizeof(address))
        );
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(socket_error(errno, "connect TCP stream"));
    }
    return TcpStream{acquired.release()};
}

TcpStream::~TcpStream() noexcept {
    close_best_effort();
}

TcpStream::TcpStream(const int descriptor) noexcept
    : descriptor_{descriptor} {}

TcpStream::TcpStream(TcpStream&& other) noexcept
    : descriptor_{std::exchange(other.descriptor_, -1)} {}

auto TcpStream::operator=(TcpStream&& other) noexcept -> TcpStream& {
    if (this != &other) {
        close_best_effort();
        descriptor_ = std::exchange(other.descriptor_, -1);
    }
    return *this;
}

auto TcpStream::is_open() const noexcept -> bool {
    return descriptor_ >= 0;
}

auto TcpStream::native_handle() const noexcept -> int {
    return descriptor_;
}

auto TcpStream::read_some(const base::MutableBytes output) const -> base::Result<std::size_t> {
    if (!is_open()) {
        return base::fail(closed_error("read TCP stream"));
    }
    if (output.empty()) {
        return 0U;
    }

    return receive_some(descriptor_, output, 0, false);
}

auto TcpStream::try_read_some(const base::MutableBytes output) const -> base::Result<std::size_t> {
    if (!is_open()) {
        return base::fail(closed_error("read TCP stream"));
    }
    if (output.empty()) {
        return 0U;
    }
    return receive_some(descriptor_, output, MSG_DONTWAIT, true);
}

auto TcpStream::read_exact(base::MutableBytes output) const -> base::Status {
    while (!output.empty()) {
        auto read = read_some(output);
        if (!read) {
            return base::fail(std::move(read).error());
        }
        if (read.value() == 0U) {
            return base::fail(
                io_error("TCP peer closed before the requested bytes arrived", "read TCP stream")
            );
        }
        output = output.subspan(read.value());
    }
    return {};
}

auto TcpStream::write_some(const base::Bytes input) const -> base::Result<std::size_t> {
    if (!is_open()) {
        return base::fail(closed_error("write TCP stream"));
    }
    if (input.empty()) {
        return 0U;
    }

    return send_some(descriptor_, input, MSG_NOSIGNAL, false);
}

auto TcpStream::try_write_some(const base::Bytes input) const -> base::Result<std::size_t> {
    if (!is_open()) {
        return base::fail(closed_error("write TCP stream"));
    }
    if (input.empty()) {
        return 0U;
    }
    return send_some(descriptor_, input, MSG_NOSIGNAL | MSG_DONTWAIT, true);
}

auto TcpStream::write_all(base::Bytes input) const -> base::Status {
    while (!input.empty()) {
        auto written = write_some(input);
        if (!written) {
            return base::fail(std::move(written).error());
        }
        if (written.value() == 0U) {
            return base::fail(io_error("TCP peer accepted zero bytes", "write TCP stream"));
        }
        input = input.subspan(written.value());
    }
    return {};
}

auto TcpStream::wait_readable(const std::chrono::milliseconds timeout) const
    -> base::Result<TcpWaitResult> {
    if (!is_open()) {
        return base::fail(closed_error("wait for TCP stream read"));
    }
    return wait_for_socket_events(descriptor_, POLLIN, timeout, "wait for TCP stream read");
}

auto TcpStream::wait_writable(const std::chrono::milliseconds timeout) const
    -> base::Result<TcpWaitResult> {
    if (!is_open()) {
        return base::fail(closed_error("wait for TCP stream write"));
    }
    return wait_for_socket_events(descriptor_, POLLOUT, timeout, "wait for TCP stream write");
}

auto TcpStream::set_no_delay(const bool enabled) const -> base::Status {
    if (!is_open()) {
        return base::fail(closed_error("set TCP_NODELAY"));
    }
    const int value = enabled ? 1 : 0;
    int result = -1;
    do {
        result = ::setsockopt(
            descriptor_,
            IPPROTO_TCP,
            TCP_NODELAY,
            &value,
            static_cast<socklen_t>(sizeof(value))
        );
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(socket_error(errno, "set TCP_NODELAY"));
    }
    return {};
}

auto TcpStream::close() -> base::Status {
    if (!is_open()) {
        return {};
    }
    const int descriptor = std::exchange(descriptor_, -1);
    if (::close(descriptor) != 0) {
        return base::fail(socket_error(errno, "close TCP stream"));
    }
    return {};
}

void TcpStream::close_best_effort() noexcept {
    if (is_open()) {
        const int descriptor = std::exchange(descriptor_, -1);
        static_cast<void>(::close(descriptor));
    }
}

auto TcpListener::bind(const TcpEndpoint& endpoint, const std::size_t backlog)
    -> base::Result<TcpListener> {
    if (backlog == 0U || backlog > static_cast<std::size_t>(INT_MAX)) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "TCP listener backlog must fit a positive int",
            }
                .with_context("io.tcp", "bind TCP listener")
        );
    }

    int descriptor = -1;
    do {
        descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        return base::fail(socket_error(errno, "create TCP listener"));
    }
    internal::UniqueFileDescriptor acquired{descriptor};

    auto close_on_exec = set_close_on_exec(acquired.get(), "set TCP listener close-on-exec");
    if (!close_on_exec) {
        return base::fail(std::move(close_on_exec).error());
    }

    // Keep the listening descriptor nonblocking permanently. accept4's
    // SOCK_NONBLOCK flag applies to the newly accepted stream, not to the
    // accept operation itself; this is what makes try_accept() genuinely a
    // one-syscall nonblocking operation without toggling shared descriptor
    // state between callers.
    auto nonblocking = set_blocking_mode(acquired.get(), false, "set TCP listener nonblocking");
    if (!nonblocking) {
        return base::fail(std::move(nonblocking).error());
    }

    const int reuse_address = 1;
    if (::setsockopt(
            acquired.get(),
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse_address,
            static_cast<socklen_t>(sizeof(reuse_address))
        )
        != 0) {
        return base::fail(socket_error(errno, "set TCP listener SO_REUSEADDR"));
    }

    const sockaddr_in address = as_sockaddr(endpoint);
    int result = -1;
    do {
        result = ::bind(
            acquired.get(),
            as_socket_address(address),
            static_cast<socklen_t>(sizeof(address))
        );
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(socket_error(errno, "bind TCP listener"));
    }

    do {
        result = ::listen(acquired.get(), static_cast<int>(backlog));
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return base::fail(socket_error(errno, "listen TCP listener"));
    }
    return TcpListener{acquired.release()};
}

TcpListener::~TcpListener() noexcept {
    close_best_effort();
}

TcpListener::TcpListener(const int descriptor) noexcept
    : descriptor_{descriptor} {}

TcpListener::TcpListener(TcpListener&& other) noexcept
    : descriptor_{std::exchange(other.descriptor_, -1)} {}

auto TcpListener::operator=(TcpListener&& other) noexcept -> TcpListener& {
    if (this != &other) {
        close_best_effort();
        descriptor_ = std::exchange(other.descriptor_, -1);
    }
    return *this;
}

auto TcpListener::is_open() const noexcept -> bool {
    return descriptor_ >= 0;
}

auto TcpListener::native_handle() const noexcept -> int {
    return descriptor_;
}

auto TcpListener::local_endpoint() const -> base::Result<TcpEndpoint> {
    if (!is_open()) {
        return base::fail(closed_error("get TCP listener endpoint"));
    }
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (::getsockname(descriptor_, as_socket_address(address), &length) != 0) {
        return base::fail(socket_error(errno, "get TCP listener endpoint"));
    }
    return endpoint_from_sockaddr(address, length);
}

auto TcpListener::wait_readable(const std::chrono::milliseconds timeout) const
    -> base::Result<TcpWaitResult> {
    if (!is_open()) {
        return base::fail(closed_error("wait for TCP listener"));
    }
    return wait_for_socket_events(descriptor_, POLLIN, timeout, "wait for TCP listener");
}

auto TcpListener::try_accept() const -> base::Result<TcpStream> {
    if (!is_open()) {
        return base::fail(closed_error("accept TCP stream"));
    }

    int descriptor = -1;
    do {
        descriptor = ::accept4(descriptor_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        if (would_block_errno(errno)) {
            return base::fail(would_block_error(errno, "accept TCP stream"));
        }
        return base::fail(socket_error(errno, "accept TCP stream"));
    }
    internal::UniqueFileDescriptor acquired{descriptor};

    auto blocking = set_blocking_mode(acquired.get(), true, "set accepted TCP stream blocking");
    if (!blocking) {
        return base::fail(std::move(blocking).error());
    }
    return TcpStream{acquired.release()};
}

auto TcpListener::accept() const -> base::Result<TcpStream> {
    if (!is_open()) {
        return base::fail(closed_error("accept TCP stream"));
    }

    while (true) {
        auto accepted = try_accept();
        if (accepted) {
            return accepted;
        }
        if (accepted.error().code() != base::ErrorCode::kUnavailable) {
            return base::fail(std::move(accepted).error());
        }
        // The listener itself stays nonblocking so try_accept() has no hidden
        // blocking path. This legacy blocking facade waits until the queue is
        // readable and then retries, including after a readiness race.
        auto ready = wait_readable(std::chrono::milliseconds{INT_MAX});
        if (!ready) {
            return base::fail(std::move(ready).error());
        }
    }
}

auto TcpListener::close() -> base::Status {
    if (!is_open()) {
        return {};
    }
    const int descriptor = std::exchange(descriptor_, -1);
    if (::close(descriptor) != 0) {
        return base::fail(socket_error(errno, "close TCP listener"));
    }
    return {};
}

void TcpListener::close_best_effort() noexcept {
    if (is_open()) {
        const int descriptor = std::exchange(descriptor_, -1);
        static_cast<void>(::close(descriptor));
    }
}

}  // namespace db::io
