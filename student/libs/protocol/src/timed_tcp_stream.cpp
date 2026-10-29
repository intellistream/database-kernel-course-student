#include "timed_tcp_stream.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "db/base/error.hpp"

namespace db::protocol::internal {
namespace {

auto timed_io_error(
    const base::ErrorCode code,
    std::string message,
    const std::string_view operation
) -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "protocol.timed_tcp_stream",
        std::string{operation}
    );
}

auto timeout_error(const std::string_view operation) -> base::Error {
    return timed_io_error(
        base::ErrorCode::kUnavailable,
        "RMDB peer did not make I/O progress before its deadline",
        operation
    );
}

auto shutdown_error(const std::string_view operation) -> base::Error {
    return timed_io_error(
        base::ErrorCode::kCancelled,
        "RMDB server shutdown was requested",
        operation
    );
}

auto positive_milliseconds(const std::chrono::steady_clock::duration duration) noexcept
    -> std::chrono::milliseconds {
    if (duration <= std::chrono::steady_clock::duration::zero()) {
        return std::chrono::milliseconds{1};
    }
    const auto truncated = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
    return std::max(std::chrono::milliseconds{1}, truncated);
}

}  // namespace

TimedTcpStream::TimedTcpStream(
    io::TcpStream& stream,
    const std::atomic_bool& shutdown_requested,
    const std::chrono::milliseconds response_write_timeout,
    const std::chrono::milliseconds shutdown_poll_interval
) noexcept
    : stream_{&stream},
      shutdown_requested_{&shutdown_requested},
      response_write_timeout_{response_write_timeout},
      shutdown_poll_interval_{shutdown_poll_interval} {}

auto TimedTcpStream::read_some_until(
    const base::MutableBytes output,
    const std::chrono::steady_clock::time_point deadline,
    const std::string_view operation
) const -> base::Result<std::size_t> {
    if (output.empty()) {
        return 0U;
    }
    while (true) {
        auto ready = wait_until_ready(Readiness::kReadable, deadline, operation);
        if (!ready) {
            return base::fail(std::move(ready).error());
        }
        auto received = stream_->try_read_some(output);
        if (received) {
            return received;
        }
        if (received.error().code() != base::ErrorCode::kUnavailable) {
            return base::fail(
                std::move(received).error().with_context(
                    "protocol.timed_tcp_stream",
                    std::string{operation}
                )
            );
        }
    }
}

auto TimedTcpStream::read_exact_until(
    base::MutableBytes output,
    const std::chrono::steady_clock::time_point deadline,
    const std::string_view operation
) const -> base::Status {
    while (!output.empty()) {
        auto received = read_some_until(output, deadline, operation);
        if (!received) {
            return base::fail(std::move(received).error());
        }
        if (received.value() == 0U) {
            return base::fail(timed_io_error(
                base::ErrorCode::kIo,
                "TCP peer closed before the requested bytes arrived",
                operation
            ));
        }
        output = output.subspan(received.value());
    }
    return {};
}

auto TimedTcpStream::write_all(base::Bytes input, const std::string_view operation) const
    -> base::Status {
    const auto deadline = std::chrono::steady_clock::now() + response_write_timeout_;
    while (!input.empty()) {
        auto ready = wait_until_ready(Readiness::kWritable, deadline, operation);
        if (!ready) {
            return base::fail(std::move(ready).error());
        }
        auto written = stream_->try_write_some(input);
        if (!written) {
            if (written.error().code() == base::ErrorCode::kUnavailable) {
                continue;
            }
            return base::fail(
                std::move(written).error().with_context(
                    "protocol.timed_tcp_stream",
                    std::string{operation}
                )
            );
        }
        if (written.value() == 0U) {
            return base::fail(
                timed_io_error(base::ErrorCode::kIo, "TCP peer accepted zero bytes", operation)
            );
        }
        input = input.subspan(written.value());
    }
    return {};
}

auto TimedTcpStream::wait_until_ready(
    const Readiness readiness,
    const std::chrono::steady_clock::time_point deadline,
    const std::string_view operation
) const -> base::Status {
    while (true) {
        if (shutdown_requested_->load(std::memory_order_acquire)) {
            return base::fail(shutdown_error(operation));
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return base::fail(timeout_error(operation));
        }
        const auto wait_time =
            std::min(positive_milliseconds(deadline - now), shutdown_poll_interval_);
        auto ready = readiness == Readiness::kReadable ? stream_->wait_readable(wait_time)
                                                       : stream_->wait_writable(wait_time);
        if (!ready) {
            return base::fail(
                std::move(ready).error().with_context(
                    "protocol.timed_tcp_stream",
                    std::string{operation}
                )
            );
        }
        if (ready.value() == io::TcpWaitResult::kReady) {
            return {};
        }
    }
}

}  // namespace db::protocol::internal
