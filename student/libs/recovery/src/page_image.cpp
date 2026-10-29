#include <cstdint>
#include <utility>

#include "page_image_internal.hpp"

namespace db::recovery::detail {
auto make_page_image_registration(
    const storage::Page& page,
    const bool standard_page,
    const wal::WalPageImagePolicy policy,
    const bool keep_delta
) -> base::Result<wal::WalPageImageRegistration> {
    auto free_space = page.free_space_view();
    if (!free_space) {
        return base::fail(
            std::move(free_space)
                .error()
                .with_context("recovery.page_image", "validate registered page")
        );
    }
    auto header = page.header();
    if (!header) {
        return base::fail(
            std::move(header).error().with_context(
                "recovery.page_image",
                "read registered page header"
            )
        );
    }

    wal::WalPageImageHole hole{};
    if (standard_page && !free_space->empty()) {
        hole = wal::WalPageImageHole{
            .offset = header->lower,
            .length = static_cast<std::uint16_t>(header->upper - header->lower),
        };
    }
    return wal::WalPageImageRegistration{
        .image = &page.bytes(),
        .page_lsn = header->lsn,
        .hole = hole,
        .policy = policy,
        .keep_delta = keep_delta,
    };
}

}  // namespace db::recovery::detail
