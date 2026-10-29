#include "db/access/heap_fetch.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "db/access/heap_table.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/page.hpp"
#include "db/transaction/ssi.hpp"
#include "db/transaction/visibility.hpp"
#include "db/transaction/xid.hpp"

#include "heap_page_scan_view.hpp"
#include "heap_tuple_state_internal.hpp"

namespace db::access {
namespace {

[[maybe_unused]] auto fetch_error(const base::ErrorCode code, std::string message) -> base::Error {
    return base::Error{code, std::move(message)}.with_context("access", "fetch heap RID");
}

[[maybe_unused]] auto record_invisible_conflicts(
    [[maybe_unused]] const HeapFetchContext& context,
    [[maybe_unused]] const TupleHeader& header
) -> base::Status {
    // LAB-S09.Q05:R2003-BEGIN
    base::lab::missing_algorithm("S09.Q05:R2003");
    // LAB-S09.Q05:R2003-END
}

[[maybe_unused]] auto record_visible_read(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const base::Rid resolved_rid,
    [[maybe_unused]] const HeapFetchContext& context,
    [[maybe_unused]] const TupleHeader& header
) -> base::Status {
    // LAB-S09.Q05:R2004-BEGIN
    base::lab::missing_algorithm("S09.Q05:R2004");
    // LAB-S09.Q05:R2004-END
}

[[maybe_unused]] auto validate_fetch_arguments(
    [[maybe_unused]] const base::Rid candidate,
    [[maybe_unused]] const HeapFetchContext& context
) -> base::Status {
    // LAB-S01.Q08:R2001-BEGIN
    base::lab::missing_algorithm("S01.Q08:R2001");
    // LAB-S01.Q08:R2001-END
}

struct PinnedHeapFetchPage final {
    storage::PinGuard pin;
};

// LAB-S01.Q08:R11-BEGIN
[[maybe_unused]] auto pin_heap_fetch_page(
    [[maybe_unused]] const HeapTable& table,
    [[maybe_unused]] const base::Rid candidate
) -> base::Result<PinnedHeapFetchPage> {
    static_cast<void>(table);
    static_cast<void>(candidate);
    return base::fail(
        fetch_error(base::ErrorCode::kUnsupported, "S01.Q08:R11: physical RID/page admission")
    );
}

// LAB-S01.Q08:R11-END

struct ChainStep final {
    bool finished{false};
    std::optional<HeapFetchTuple> tuple;
};

class HotChainSearch final {
public:
    HotChainSearch(
        const HeapPageScanView& page,
        const base::RelationId relation_id,
        const base::Rid candidate,
        const HeapFetchContext context
    )
        : page_{&page},
          relation_id_{relation_id},
          candidate_{candidate},
          context_{context},
          visited_(page.item_count(), 0),
          slot_{candidate.slot_id()} {}

    [[maybe_unused]] auto run() -> base::Result<std::optional<HeapFetchTuple>> {
        // LAB-S09.Q04:R05-BEGIN
        base::lab::missing_algorithm("S09.Q04:R05");
        // LAB-S09.Q04:R05-END
    }

private:
    [[maybe_unused]] auto visit_current_slot() -> base::Result<ChainStep> {
        // LAB-S09.Q04:R06-BEGIN
        base::lab::missing_algorithm("S09.Q04:R06");
        // LAB-S09.Q04:R06-END
    }

    [[maybe_unused]] auto claim_current_slot() -> base::Status {
        // LAB-S09.Q04:R07-BEGIN
        base::lab::missing_algorithm("S09.Q04:R07");
        // LAB-S09.Q04:R07-END
    }

    [[maybe_unused]] auto follow_redirect([[maybe_unused]] const LinePointer& pointer)
        -> base::Result<ChainStep> {
        // LAB-S09.Q04:R08-BEGIN
        base::lab::missing_algorithm("S09.Q04:R08");
        // LAB-S09.Q04:R08-END
    }

    [[maybe_unused]] auto finish_at_non_normal_slot() const -> base::Result<ChainStep> {
        // LAB-S09.Q04:R09-BEGIN
        base::lab::missing_algorithm("S09.Q04:R09");
        // LAB-S09.Q04:R09-END
    }

    [[maybe_unused]] auto
    validate_predecessor_link([[maybe_unused]] const TupleHeader& header) const -> base::Status {
        // LAB-S09.Q04:R10-BEGIN
        base::lab::missing_algorithm("S09.Q04:R10");
        // LAB-S09.Q04:R10-END
    }

    [[maybe_unused]] auto is_visible([[maybe_unused]] const TupleHeader& header) const noexcept
        -> bool {
        // LAB-S09.Q03:R05-BEGIN
        base::lab::missing_algorithm("S09.Q03:R05");
        // LAB-S09.Q03:R05-END
    }

    [[maybe_unused]] auto finish_visible([[maybe_unused]] const HeapTupleView& tuple) const
        -> base::Result<ChainStep> {
        // LAB-S09.Q04:R11-BEGIN
        base::lab::missing_algorithm("S09.Q04:R11");
        // LAB-S09.Q04:R11-END
    }

    [[maybe_unused]] auto follow_hot_link([[maybe_unused]] const TupleHeader& header)
        -> base::Status {
        // LAB-S09.Q04:R12-BEGIN
        base::lab::missing_algorithm("S09.Q04:R12");
        // LAB-S09.Q04:R12-END
    }

    const HeapPageScanView* page_;
    base::RelationId relation_id_;
    base::Rid candidate_;
    HeapFetchContext context_;
    std::vector<std::uint8_t> visited_;
    base::SlotId slot_;
    base::Xid expected_xmin_{base::kInvalidXid};
    bool followed_hot_link_{false};
};

}  // namespace

auto HeapTable::fetch(const base::Rid candidate, const HeapFetchContext context) const
    -> base::Result<std::optional<HeapFetchTuple>> {
    if (!valid()) {
        return base::fail(
            fetch_error(base::ErrorCode::kUnavailable, "heap relation handle is inactive")
        );
    }
    auto valid = validate_fetch_arguments(candidate, context);
    if (!valid) {
        return base::fail(std::move(valid).error());
    }

    auto pinned = pin_heap_fetch_page(*this, candidate);
    if (!pinned) {
        return base::fail(std::move(pinned).error());
    }
    if (context.prune_xmin.has_value()) {
        auto pruned = try_prune_pinned(pinned->pin, *context.mvcc.commit_log, *context.prune_xmin);
        if (!pruned) {
            return base::fail(std::move(pruned).error());
        }
    }
    auto read = std::move(pinned).value().pin.read_latch();
    if (!read) {
        return base::fail(std::move(read).error());
    }
    auto page = HeapPageScanView::open(*read->page());
    if (!page) {
        return base::fail(std::move(page).error());
    }
    if (candidate.slot_id().value() >= page->item_count()) {
        return std::optional<HeapFetchTuple>{};
    }
    return HotChainSearch{page.value(), relation_id(), candidate, context}.run();
}

}  // namespace db::access
