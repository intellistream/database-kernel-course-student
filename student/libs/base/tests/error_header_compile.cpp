#include <string_view>
#include <type_traits>

#include "db/base/error.hpp"

static_assert(
    std::is_same_v<decltype(db::base::error_code_name(db::base::ErrorCode::kIo)), std::string_view>
);
