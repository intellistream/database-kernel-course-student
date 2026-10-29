#include <type_traits>

#include "db/recovery/recovery_page_cache.hpp"

static_assert(
    std::is_base_of_v<db::recovery::StartupPageAccessPort, db::recovery::RecoveryPageCache>
);
static_assert(std::is_move_constructible_v<db::recovery::RecoveryPageCacheOptions>);
static_assert(db::recovery::RecoveryPageCacheStats{} == db::recovery::RecoveryPageCacheStats{});
