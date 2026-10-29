#include <type_traits>

#include "proc_array_state.hpp"

static_assert(!std::is_copy_constructible_v<db::transaction::detail::ProcArrayState>);
