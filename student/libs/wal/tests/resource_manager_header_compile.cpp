#include <cstdint>
#include <type_traits>

#include "db/wal/resource_manager.hpp"

static_assert(std::is_same_v<std::underlying_type_t<db::wal::WalResourceManager>, std::uint8_t>);
static_assert(static_cast<std::uint8_t>(db::wal::WalResourceManager::kCatalog) == 4U);
