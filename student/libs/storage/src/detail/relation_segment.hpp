#pragma once

#include <cstdint>

#include "db/base/identifier.hpp"
#include "db/storage/page.hpp"

namespace db::storage::detail {

// Relation segment geometry is part of the durable relation-file format. It
// deliberately matches PostgreSQL's default 1 GiB segment size while retaining
// DB's own page size and file naming.
inline constexpr std::uint64_t kRelationSegmentPages{262'144U};
inline constexpr std::uint64_t kRelationSegmentBytes{kRelationSegmentPages * kPageSize};

struct RelationSegmentAddress final {
    std::uint64_t segment_number;
    std::uint64_t page_offset;
    std::uint64_t byte_offset;
};

constexpr auto split_relation_page_id(const base::PageId page_id) noexcept
    -> RelationSegmentAddress {
    const auto value = static_cast<std::uint64_t>(page_id.value());
    const auto page_offset = value % kRelationSegmentPages;
    return {
        .segment_number = value / kRelationSegmentPages,
        .page_offset = page_offset,
        .byte_offset = page_offset * kPageSize,
    };
}

static_assert(kRelationSegmentBytes == 1024ULL * 1024ULL * 1024ULL);

}  // namespace db::storage::detail
