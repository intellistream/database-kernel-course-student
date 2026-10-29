#include <type_traits>

#include "db/course/storage.hpp"

static_assert(std::is_same_v<db::course::storage::PageTag, db::storage::PageAddress>);
static_assert(std::is_move_constructible_v<db::course::storage::PinGuard>);
