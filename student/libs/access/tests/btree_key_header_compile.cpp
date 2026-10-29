#include <type_traits>

#include "db/access/btree_key.hpp"

static_assert(std::is_nothrow_move_constructible_v<db::access::BtreeKeyLayout>);
static_assert(std::is_nothrow_move_assignable_v<db::access::BtreeKeyLayout>);
