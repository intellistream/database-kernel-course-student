#include <type_traits>

#include "db/protocol/rmdb_client.hpp"

namespace {

static_assert(!std::is_copy_constructible_v<db::protocol::RmdbClient>);
static_assert(std::is_nothrow_move_constructible_v<db::protocol::RmdbClient>);
static_assert(std::is_copy_constructible_v<db::protocol::RmdbPreparedCatalog>);
static_assert(std::is_default_constructible_v<db::protocol::RmdbPreparedCatalog>);

}  // namespace
