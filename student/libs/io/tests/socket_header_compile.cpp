#include <type_traits>

#include "db/io/socket.hpp"

static_assert(!std::is_copy_constructible_v<db::io::TcpStream>);
static_assert(std::is_nothrow_move_constructible_v<db::io::TcpStream>);
static_assert(!std::is_copy_constructible_v<db::io::TcpListener>);
static_assert(std::is_nothrow_move_constructible_v<db::io::TcpListener>);

namespace {

[[maybe_unused]] void compile_public_tcp_api() {
    const auto endpoint = db::io::TcpEndpoint::loopback(8765U);
    static_cast<void>(endpoint.to_string());
    static_cast<void>(db::io::TcpEndpoint::parse("127.0.0.1:8765"));
    static_cast<void>(db::io::TcpListener::bind(endpoint));
    static_cast<void>(db::io::TcpStream::connect(endpoint));
}

}  // namespace
