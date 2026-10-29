#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "db/base/bytes.hpp"
#include "db/base/result.hpp"
#include "db/io/socket.hpp"

namespace db::protocol::internal {

// Applies protocol-owned absolute read deadlines and per-frame write deadlines
// to an otherwise blocking TcpStream. It uses io's nonblocking single-syscall
// operations after readiness waits, so a slow peer cannot turn a connection
// worker into an unbounded blocking writer. The stop flag is observed at each
// bounded readiness interval; ownership and socket policy remain with caller.
class TimedTcpStream final {
public:
    TimedTcpStream(
        io::TcpStream& stream,
        const std::atomic_bool& shutdown_requested,
        std::chrono::milliseconds response_write_timeout,
        std::chrono::milliseconds shutdown_poll_interval
    ) noexcept;

    auto read_some_until(
        base::MutableBytes output,
        std::chrono::steady_clock::time_point deadline,
        std::string_view operation
    ) const -> base::Result<std::size_t>;
    auto read_exact_until(
        base::MutableBytes output,
        std::chrono::steady_clock::time_point deadline,
        std::string_view operation
    ) const -> base::Status;
    auto write_all(base::Bytes input, std::string_view operation) const -> base::Status;

private:
    enum class Readiness : std::uint8_t {
        kReadable,
        kWritable,
    };

    auto wait_until_ready(
        Readiness readiness,
        std::chrono::steady_clock::time_point deadline,
        std::string_view operation
    ) const -> base::Status;

    io::TcpStream* stream_{nullptr};
    const std::atomic_bool* shutdown_requested_{nullptr};
    std::chrono::milliseconds response_write_timeout_;
    std::chrono::milliseconds shutdown_poll_interval_;
};

}  // namespace db::protocol::internal
