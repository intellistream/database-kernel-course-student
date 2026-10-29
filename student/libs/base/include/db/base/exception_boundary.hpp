#pragma once

#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "db/base/error.hpp"
#include "db/base/result.hpp"

namespace db::base {

// Stable, domain-neutral policy for translating exceptions at an owning
// adapter or injected-port boundary. Components own the policy values and the
// decision about where such a boundary belongs.
struct ExceptionBoundaryPolicy final {
    std::string_view component;
    std::string_view allocation_message;
    std::string_view exception_prefix;
    std::string_view unknown_exception_message;
};

namespace detail {

// Called only while handling the callback exception below. Keeping exception
// classification out of the template avoids instantiating it for every port.
auto current_exception_boundary_error(
    const ExceptionBoundaryPolicy& policy,
    std::string_view operation
) -> Error;

}  // namespace detail

// Returned DB failures pass through unchanged. Only exceptions escaping
// the callback are converted, so callers may still classify or enrich a
// returned error at their own abstraction boundary.
template <typename Callback>
auto invoke_exception_boundary(
    const ExceptionBoundaryPolicy& policy,
    const std::string_view operation,
    Callback&& callback
) -> std::invoke_result_t<Callback&&> {
    try {
        return std::invoke(std::forward<Callback>(callback));
    } catch (...) {
        return fail(detail::current_exception_boundary_error(policy, operation));
    }
}

template <typename Callback>
auto invoke_exception_boundary(const ExceptionBoundaryPolicy& policy, Callback&& callback)
    -> std::invoke_result_t<Callback&&> {
    return invoke_exception_boundary(policy, std::string_view{}, std::forward<Callback>(callback));
}

}  // namespace db::base
