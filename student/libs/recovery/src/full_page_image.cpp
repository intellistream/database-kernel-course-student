#include <algorithm>
#include <string>
#include <utility>

#include "db/base/error.hpp"

#include "full_page_image_internal.hpp"

namespace db::recovery::detail {
namespace {

auto image_error(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kCorruption, std::move(message)}.with_context(
        "recovery.full_page_image",
        "build candidate"
    );
}

}  // namespace

auto make_full_page_image_base(
    const wal::WalBlockReference& block,
    const storage::PageKind expected_kind
) -> base::Result<storage::Page> {
    if (!block.full_page_image.has_value()) {
        return base::fail(image_error("WAL block has no full-page image"));
    }
    const storage::PageAddress address{block.relation_id, block.page_id};
    if (!address.valid()) {
        return base::fail(image_error("full-page image has an invalid page address"));
    }
    storage::Page::Storage image{};
    std::copy(block.full_page_image->begin(), block.full_page_image->end(), image.begin());
    auto candidate = storage::Page::load(image, address);
    if (!candidate) {
        return base::fail(
            std::move(candidate).error().with_context("recovery.full_page_image", "load image")
        );
    }
    auto header = candidate->header();
    if (!header || header->kind != expected_kind) {
        return base::fail(image_error("full-page image has an unexpected page kind"));
    }
    const auto& hole = block.image_hole;
    if ((!block.standard_page && (hole.offset != 0 || hole.length != 0))
        || (hole.length == 0 && hole.offset != 0)) {
        return base::fail(image_error("full-page image has invalid page-hole metadata"));
    }
    if (hole.length != 0
        && (hole.offset != header->lower || hole.length != header->upper - header->lower)) {
        return base::fail(image_error("full-page image hole disagrees with its page header"));
    }
    return candidate;
}

}  // namespace db::recovery::detail
