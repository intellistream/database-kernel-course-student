#pragma once

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace db::base::lab {

[[noreturn]] inline void missing_algorithm(const std::string_view region_id) noexcept {
    static constexpr std::string_view prefix{"Ferrum student lab missing: "};
    static_cast<void>(std::fwrite(prefix.data(), 1, prefix.size(), stderr));
    static_cast<void>(std::fwrite(region_id.data(), 1, region_id.size(), stderr));
    static_cast<void>(std::fputc('\n', stderr));
    std::abort();
}

}  // namespace db::base::lab
