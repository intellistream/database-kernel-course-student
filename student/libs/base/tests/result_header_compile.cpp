#include <type_traits>

#include "db/base/result.hpp"

static_assert(std::is_same_v<db::base::Status, db::base::Result<void>>);
