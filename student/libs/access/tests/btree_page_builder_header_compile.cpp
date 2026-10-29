#include <type_traits>

#include "btree_page_builder.hpp"

namespace {

static_assert(std::is_aggregate_v<db::access::BtreeBulkLeafEntry>);
static_assert(std::is_aggregate_v<db::access::BtreeBulkInternalEntry>);

}  // namespace
