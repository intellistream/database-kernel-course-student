#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>

#include "db/base/result.hpp"
#include "db/io/socket.hpp"
#include "db/protocol/tcp_server.hpp"

namespace db::server {

inline constexpr std::size_t kDefaultServerBufferPoolPages = 524'288U;

struct ServerOptions final {
    std::filesystem::path data_directory{"db-data"};
    io::TcpEndpoint endpoint{io::TcpEndpoint::loopback(8765U)};
    std::size_t buffer_pool_pages{kDefaultServerBufferPoolPages};
    protocol::TcpServerOptions tcp{};
};

// Parses the reference-compatible foreground-server inputs: one optional data
// directory, DB_ADDRESS for a numeric IPv4 endpoint, and
// DB_BUFFER_POOL_PAGES for a positive cache size. This is startup-only:
// callers must not mutate the process environment concurrently with this call.
auto parse_server_options(std::span<char*> arguments) -> base::Result<ServerOptions>;

// Owns the database, application adapter, and protocol server in lifetime
// order. serve_forever() joins all connection workers before it returns;
// callers must request shutdown and wait for that return before destroying the
// application or invoking shutdown(). Explicit shutdown errors retain the
// remaining database owner so callers can retry; consequently a live
// application cannot be replaced by move assignment.
class ServerApplication final {
public:
    static auto open(const ServerOptions& options) -> base::Result<ServerApplication>;

    ServerApplication(ServerApplication&& other) noexcept;
    auto operator=(ServerApplication&& other) -> ServerApplication& = delete;
    ServerApplication(const ServerApplication&) = delete;
    auto operator=(const ServerApplication&) -> ServerApplication& = delete;
    ~ServerApplication();

    auto local_endpoint() const -> base::Result<io::TcpEndpoint>;
    auto serve_forever() -> base::Status;
    void request_shutdown() noexcept;

    // Ends the foreground lifecycle after serve_forever() has returned. It
    // releases the TcpServer and adapter before closing the durable database.
    auto shutdown() -> base::Status;

private:
    struct Impl;

    explicit ServerApplication(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::server
