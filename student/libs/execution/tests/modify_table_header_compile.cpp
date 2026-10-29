#include <type_traits>

#include "db/execution/modify_table.hpp"

static_assert(!std::is_copy_constructible_v<db::execution::ModifyTableState>);
static_assert(!std::is_copy_constructible_v<db::execution::ModifyTablePort>);
