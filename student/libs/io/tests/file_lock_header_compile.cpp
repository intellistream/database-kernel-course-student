#include <type_traits>

#include "db/io/file_lock.hpp"

static_assert(!std::is_copy_constructible_v<db::io::ExclusiveFileLock>);
static_assert(std::is_nothrow_move_constructible_v<db::io::ExclusiveFileLock>);
