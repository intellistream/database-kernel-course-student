#pragma once

#include <string_view>
#include <utility>

#include "db/base/exception_boundary.hpp"

namespace db::recovery::detail {

inline constexpr base::ExceptionBoundaryPolicy kStartupPortExceptionPolicy{
    .component = "recovery.startup_port",
    .allocation_message = "startup recovery port exhausted available memory",
    .exception_prefix = "startup recovery port threw an exception: ",
    .unknown_exception_message = "startup recovery port threw a non-standard exception",
};

template <typename Callback>
auto invoke_startup_port(const std::string_view operation, Callback&& callback) {
    return base::invoke_exception_boundary(
        kStartupPortExceptionPolicy,
        operation,
        std::forward<Callback>(callback)
    );
}

}  // namespace db::recovery::detail
