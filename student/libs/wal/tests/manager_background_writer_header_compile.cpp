#include <type_traits>

#include "manager_background_writer.hpp"

static_assert(!std::is_copy_constructible_v<db::wal::manager_detail::ManagerBackgroundWriter>);
static_assert(!std::is_move_constructible_v<db::wal::manager_detail::ManagerBackgroundWriter>);
