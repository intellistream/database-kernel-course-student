#include <type_traits>

#include "db/execution/modify_port.hpp"

namespace {

static_assert(std::is_copy_constructible_v<db::execution::ModifyTarget>);
static_assert(!std::is_copy_constructible_v<db::execution::ModifyTablePort>);

}  // namespace
