#include <type_traits>

#include "db/access/tuple_codec.hpp"

static_assert(std::is_move_constructible_v<db::access::TupleSchema>);
