#include <type_traits>

#include "db/access/btree_relation.hpp"
#include "db/access/heap_index_tid.hpp"
#include "db/access/index_tid.hpp"

static_assert(std::is_abstract_v<db::access::IndexTidClassifier>);
static_assert(std::is_abstract_v<db::access::UniqueConflictWaiter>);
static_assert(!std::is_copy_constructible_v<db::access::HeapIndexTidClassifier>);
static_assert(std::is_nothrow_move_constructible_v<db::access::HeapIndexTidClassifier>);
static_assert(std::is_nothrow_move_assignable_v<db::access::HeapIndexTidClassifier>);
