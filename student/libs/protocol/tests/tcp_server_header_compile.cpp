#include <type_traits>

#include "db/protocol/tcp_server.hpp"

static_assert(!std::is_copy_constructible_v<db::protocol::SessionPort>);
static_assert(!std::is_copy_constructible_v<db::protocol::SessionFactoryPort>);
static_assert(!std::is_copy_constructible_v<db::protocol::TcpServer>);
static_assert(std::is_nothrow_move_constructible_v<db::protocol::TcpServer>);
