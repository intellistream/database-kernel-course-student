#include "detail/relation_segment.hpp"

namespace {

using db::base::PageId;
using db::storage::detail::kRelationSegmentBytes;
using db::storage::detail::kRelationSegmentPages;
using db::storage::detail::split_relation_page_id;

static_assert(kRelationSegmentPages == 262'144U);
static_assert(kRelationSegmentBytes == 1024ULL * 1024ULL * 1024ULL);
static_assert(split_relation_page_id(PageId{0}).segment_number == 0U);
static_assert(split_relation_page_id(PageId{0}).byte_offset == 0U);
static_assert(split_relation_page_id(PageId{262'144}).segment_number == 1U);
static_assert(split_relation_page_id(PageId{262'144}).byte_offset == 0U);

}  // namespace
