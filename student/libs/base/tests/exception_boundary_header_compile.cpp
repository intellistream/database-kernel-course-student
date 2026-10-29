#include "db/base/exception_boundary.hpp"

namespace {

[[maybe_unused]] void use_exception_boundary_header() {
    constexpr db::base::ExceptionBoundaryPolicy policy{
        .component = "base.header_check",
        .allocation_message = "allocation failed",
        .exception_prefix = "exception: ",
        .unknown_exception_message = "unknown exception",
    };
    auto result =
        db::base::invoke_exception_boundary(policy, "invoke", []() -> db::base::Result<int> {
            return 42;
        });
    (void)result;
}

}  // namespace
