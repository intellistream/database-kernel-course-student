#include <type_traits>

#include "db/course/access.hpp"

static_assert(std::is_same_v<db::course::access::BPlusTree, db::access::BtreeRelation>);
static_assert(std::is_move_constructible_v<db::course::access::HeapScan>);
