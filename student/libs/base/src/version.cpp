#include "db/base/version.hpp"

#include <string_view>

namespace db::base {

auto version_string() noexcept -> std::string_view {
    return "0.1.0";
}

}  // namespace db::base
