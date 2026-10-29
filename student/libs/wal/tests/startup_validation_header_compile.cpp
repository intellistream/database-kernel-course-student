#include <type_traits>

#include "db/wal/startup_validation.hpp"

static_assert(!std::is_default_constructible_v<db::wal::WalStartupValidation>);
static_assert(std::is_copy_constructible_v<db::wal::WalStartupValidation>);
