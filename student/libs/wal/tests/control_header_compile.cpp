#include <cstdint>
#include <type_traits>

#include "db/wal/control.hpp"

static_assert(std::is_same_v<std::underlying_type_t<db::wal::DatabaseState>, std::uint8_t>);
static_assert(db::wal::WalControlImage{}.state == db::wal::DatabaseState::kShutdown);
