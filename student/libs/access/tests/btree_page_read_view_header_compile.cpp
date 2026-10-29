#include <type_traits>

#include "btree_page_read_view.hpp"

static_assert(std::is_move_constructible_v<db::access::BtreePageReadView>);
