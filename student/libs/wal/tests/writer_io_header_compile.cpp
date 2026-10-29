#include <type_traits>

#include "writer_io.hpp"

static_assert(!std::is_copy_constructible_v<db::wal::writer_detail::WriterIoCoordinator>);
static_assert(!std::is_move_constructible_v<db::wal::writer_detail::WriterIoCoordinator>);
static_assert(std::is_move_constructible_v<db::wal::writer_detail::WriterIoCoordinator::Leader>);
