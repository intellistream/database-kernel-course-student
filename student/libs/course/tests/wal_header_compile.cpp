#include <type_traits>

#include "db/course/wal.hpp"

static_assert(std::is_move_constructible_v<db::course::wal::WalReader>);
static_assert(std::is_same_v<db::course::wal::WalRecord, db::wal::WalRecord>);
