#include <cstdint>

#include "db/base/checked.hpp"

static_assert(requires { db::base::checked_add<std::uint32_t>(1U, 2U); });
static_assert(requires { db::base::checked_cast<std::uint16_t>(1U); });
