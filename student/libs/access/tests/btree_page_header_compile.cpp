#include <type_traits>

#include "db/access/btree_page.hpp"

static_assert(std::is_move_constructible_v<db::access::BtreeMetaPage>);
static_assert(std::is_move_constructible_v<db::access::BtreePage>);
static_assert(std::is_trivially_copyable_v<db::access::BtreeRoot>);
