#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/access/heap_page.hpp"
#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"
#include "db/storage/page.hpp"
#include "db/transaction/xid.hpp"

namespace db::access {
namespace {

[[maybe_unused]] auto prune_error(
    const base::ErrorCode code,
    const std::string_view operation,
    const std::string_view message
) -> base::Error {
    return base::Error{code, std::string{message}}.with_context("access", std::string{operation});
}

[[maybe_unused]] auto valid_prune_horizon(const base::Xid oldest_xmin) noexcept -> bool {
    return transaction::is_normal_xid(oldest_xmin) || oldest_xmin == transaction::kXidWrapBoundary;
}

[[maybe_unused]] auto committed_before_horizon(
    const base::Xid xid,
    const transaction::CommitLog& commit_log,
    const base::Xid oldest_xmin
) -> bool {
    return transaction::is_normal_xid(xid) && xid.value() < oldest_xmin.value()
           && commit_log.is_committed(xid);
}

[[maybe_unused]] auto terminal_is_removable(
    const TupleHeader& header,
    const transaction::CommitLog& commit_log,
    const base::Xid oldest_xmin
) -> bool {
    const auto removes_version =
        header.has_flag(tuple_flags::kDeleted) || header.has_flag(tuple_flags::kUpdated);
    return removes_version && committed_before_horizon(header.xmax, commit_log, oldest_xmin);
}

struct CompactionTuple final {
    base::SlotId slot{base::kInvalidSlotId};
    std::vector<std::byte> bytes;
};

struct HotChainPlan final {
    base::SlotId terminal{base::kInvalidSlotId};
    std::vector<base::SlotId> intermediates;
    bool advanced{false};
};

class PruneCandidates final {
public:
    explicit PruneCandidates(const std::size_t count)
        : dead_(count, 0U),
          unused_(count, 0U) {}

    [[maybe_unused]] void mark_dead(const base::SlotId slot) noexcept {
        dead_[slot.value()] = 1U;
        unused_[slot.value()] = 0U;
    }

    [[maybe_unused]] void mark_unused(const base::SlotId slot) noexcept {
        if (!is_dead(slot)) {
            unused_[slot.value()] = 1U;
        }
    }

    [[maybe_unused]] auto is_dead(const base::SlotId slot) const noexcept -> bool {
        return dead_[slot.value()] != 0U;
    }

    [[maybe_unused]] auto is_unused(const base::SlotId slot) const noexcept -> bool {
        return unused_[slot.value()] != 0U;
    }

private:
    std::vector<std::uint8_t> dead_;
    std::vector<std::uint8_t> unused_;
};

}  // namespace

class HeapPage::CompactionOperation final {
public:
    explicit CompactionOperation(HeapPage& page) noexcept
        : page_{page} {}

    [[maybe_unused]] auto run() -> base::Result<std::size_t> {
        // LAB-S01.Q07:R07-BEGIN
        return base::fail(
            base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R07: algorithm not implemented"}
        );
        // LAB-S01.Q07:R07-END
    }

private:
    [[maybe_unused]] auto collect_tuples() -> base::Status {
        // LAB-S01.Q07:R08-BEGIN
        return base::fail(
            base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R08: algorithm not implemented"}
        );
        // LAB-S01.Q07:R08-END
    }

    [[maybe_unused]] auto append_tuple([[maybe_unused]] const base::SlotId slot) -> base::Status {
        // LAB-S01.Q07:R09-BEGIN
        static_cast<void>(slot);
        return base::fail(
            base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R09: algorithm not implemented"}
        );
        // LAB-S01.Q07:R09-END
    }

    [[maybe_unused]] auto plan_layout() -> base::Status {
        // LAB-S01.Q07:R10-BEGIN
        return base::fail(
            base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R10: algorithm not implemented"}
        );
        // LAB-S01.Q07:R10-END
    }

    [[maybe_unused]] auto write_compacted_page() -> base::Status {
        // LAB-S01.Q07:R11-BEGIN
        return base::fail(
            base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R11: algorithm not implemented"}
        );
        // LAB-S01.Q07:R11-END
    }

    [[maybe_unused]] auto write_tuple(
        [[maybe_unused]] const CompactionTuple& tuple,
        [[maybe_unused]] const std::size_t offset
    ) -> base::Status {
        // LAB-S01.Q07:R12-BEGIN
        static_cast<void>(tuple);
        static_cast<void>(offset);
        return base::fail(
            base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R12: algorithm not implemented"}
        );
        // LAB-S01.Q07:R12-END
    }

    [[maybe_unused]] auto finish_page_header() -> base::Status {
        // LAB-S01.Q07:R13-BEGIN
        return base::fail(
            base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R13: algorithm not implemented"}
        );
        // LAB-S01.Q07:R13-END
    }

    HeapPage& page_;
    storage::PageHeader header_{};
    std::vector<CompactionTuple> tuples_;
    std::optional<std::size_t> final_used_{};
    std::size_t total_tuple_bytes_{0};
    std::size_t new_lower_{0};
    std::size_t new_upper_{0};
};

class HeapPage::PruneOperation final {
public:
    PruneOperation(
        HeapPage& page,
        const transaction::CommitLog& commit_log,
        const base::Xid oldest_xmin
    )
        : page_{page},
          commit_log_{commit_log},
          oldest_xmin_{oldest_xmin},
          before_{*page.page_},
          count_{page.item_count()},
          candidates_{count_} {}

    auto run() -> base::Result<HeapPruneResult> {
        auto aborted = plan_aborted_versions();
        if (!aborted) {
            return base::fail(std::move(aborted).error());
        }
        auto roots = plan_roots();
        if (!roots) {
            return base::fail(std::move(roots).error());
        }
        auto applied = apply_changes();
        if (!applied) {
            *page_.page_ = before_;
            return base::fail(std::move(applied).error());
        }
        return std::move(result_);
    }

private:
    auto read_header(const base::SlotId slot) const -> base::Result<TupleHeader> {
        auto tuple = page_.tuple(slot);
        if (!tuple) {
            return base::fail(std::move(tuple).error());
        }
        return tuple->header;
    }

    auto prunable_hot(const TupleHeader& header) const -> bool {
        return header.has_flag(tuple_flags::kHotUpdated) && header.has_flag(tuple_flags::kUpdated)
               && committed_before_horizon(header.xmax, commit_log_, oldest_xmin_);
    }

    auto plan_aborted_versions() -> base::Status {
        for (std::size_t index = 0; index < count_; ++index) {
            auto planned = plan_aborted_slot(base::SlotId{static_cast<std::uint16_t>(index)});
            if (!planned) {
                return planned;
            }
        }
        return {};
    }

    auto plan_aborted_slot(const base::SlotId slot) -> base::Status {
        auto pointer = page_.line_pointer(slot);
        if (!pointer) {
            return base::fail(std::move(pointer).error());
        }
        if (pointer->state != LinePointerState::kNormal) {
            return {};
        }
        auto header = read_header(slot);
        if (!header) {
            return base::fail(std::move(header).error());
        }
        if (!transaction::is_normal_xid(header->xmin) || !commit_log_.is_aborted(header->xmin)) {
            return {};
        }
        if (header->has_flag(tuple_flags::kHeapOnly)) {
            candidates_.mark_unused(slot);
        } else {
            candidates_.mark_dead(slot);
        }
        return {};
    }

    auto plan_roots() -> base::Status {
        for (std::size_t index = 0; index < count_; ++index) {
            auto planned = plan_root(base::SlotId{static_cast<std::uint16_t>(index)});
            if (!planned) {
                return planned;
            }
        }
        return {};
    }

    auto plan_root(const base::SlotId root) -> base::Status {
        auto pointer = page_.line_pointer(root);
        if (!pointer) {
            return base::fail(std::move(pointer).error());
        }
        if (pointer->state == LinePointerState::kRedirect) {
            return plan_redirect_root(root, base::SlotId{pointer->offset});
        }
        if (pointer->state != LinePointerState::kNormal || candidates_.is_dead(root)) {
            return {};
        }
        return plan_normal_root(root);
    }

    auto plan_redirect_root(const base::SlotId root, const base::SlotId target) -> base::Status {
        auto chain = walk_chain(target, std::nullopt);
        if (!chain) {
            return base::fail(std::move(chain).error());
        }
        return plan_chain_result(root, chain.value(), true);
    }

    auto plan_normal_root(const base::SlotId root) -> base::Status {
        auto header = read_header(root);
        if (!header) {
            return base::fail(std::move(header).error());
        }
        if (header->has_flag(tuple_flags::kHeapOnly)) {
            return {};
        }
        if (!prunable_hot(header.value())) {
            if (terminal_is_removable(header.value(), commit_log_, oldest_xmin_)) {
                candidates_.mark_dead(root);
            }
            return {};
        }
        auto valid_link = validate_hot_link(header.value(), "heap HOT root");
        if (!valid_link) {
            return valid_link;
        }
        auto chain = walk_chain(header->ctid.slot_id(), header->xmax);
        if (!chain) {
            return base::fail(std::move(chain).error());
        }
        return plan_chain_result(root, chain.value(), false);
    }

    auto plan_chain_result(
        const base::SlotId root,
        const HotChainPlan& chain,
        const bool root_is_redirect
    ) -> base::Status {
        auto terminal = read_header(chain.terminal);
        if (!terminal) {
            return base::fail(std::move(terminal).error());
        }
        if (terminal_is_removable(terminal.value(), commit_log_, oldest_xmin_)) {
            candidates_.mark_dead(root);
            mark_intermediates_unused(chain);
            candidates_.mark_unused(chain.terminal);
            return {};
        }
        if (!root_is_redirect || chain.advanced) {
            redirects_.push_back(HeapPruneRedirect{.root = root, .target = chain.terminal});
            mark_intermediates_unused(chain);
        }
        return {};
    }

    void mark_intermediates_unused(const HotChainPlan& chain) noexcept {
        for (const auto slot : chain.intermediates) {
            candidates_.mark_unused(slot);
        }
    }

    auto walk_chain(base::SlotId current, std::optional<base::Xid> expected_xmin) const
        -> base::Result<HotChainPlan> {
        HotChainPlan plan;
        std::vector<std::uint8_t> visited(count_, 0U);
        for (std::size_t hops = 0; hops < count_; ++hops) {
            auto header = validate_chain_member(current, expected_xmin, visited);
            if (!header) {
                return base::fail(std::move(header).error());
            }
            if (!prunable_hot(header.value())) {
                plan.terminal = current;
                return plan;
            }
            auto valid_link = validate_hot_link(header.value(), "heap HOT chain");
            if (!valid_link) {
                return base::fail(std::move(valid_link).error());
            }
            plan.intermediates.push_back(current);
            plan.advanced = true;
            expected_xmin = header->xmax;
            current = header->ctid.slot_id();
        }
        return base::fail(prune_error(
            base::ErrorCode::kCorruption,
            "prune heap page",
            "heap HOT chain exceeds the line pointer bound"
        ));
    }

    auto validate_chain_member(
        const base::SlotId current,
        const std::optional<base::Xid> expected_xmin,
        std::vector<std::uint8_t>& visited
    ) const -> base::Result<TupleHeader> {
        if (current.value() >= count_ || visited[current.value()] != 0U) {
            return base::fail(prune_error(
                base::ErrorCode::kCorruption,
                "prune heap page",
                "heap HOT chain is out of range or cyclic"
            ));
        }
        visited[current.value()] = 1U;
        auto pointer = page_.line_pointer(current);
        if (!pointer || pointer->state != LinePointerState::kNormal) {
            return base::fail(prune_error(
                base::ErrorCode::kCorruption,
                "prune heap page",
                "heap HOT chain reaches a non-normal line pointer"
            ));
        }
        auto header = read_header(current);
        if (!header) {
            return base::fail(std::move(header).error());
        }
        if (!header->has_flag(tuple_flags::kHeapOnly)
            || (expected_xmin.has_value() && header->xmin != expected_xmin.value())) {
            return base::fail(prune_error(
                base::ErrorCode::kCorruption,
                "prune heap page",
                "heap HOT chain has inconsistent heap-only or adjacent-XID metadata"
            ));
        }
        return header.value();
    }

    auto validate_hot_link(const TupleHeader& header, const std::string_view subject) const
        -> base::Status {
        if (header.ctid.page_id() == page_.address_.page_id
            && header.ctid.slot_id() != base::kInvalidSlotId) {
            return {};
        }
        return base::fail(prune_error(
            base::ErrorCode::kCorruption,
            "prune heap page",
            std::string{subject} + " crosses a page or has an invalid successor"
        ));
    }

    auto apply_changes() -> base::Status {
        auto redirects = apply_redirects();
        if (!redirects) {
            return redirects;
        }
        auto dead = apply_dead();
        if (!dead) {
            return dead;
        }
        auto unused = apply_unused();
        if (!unused) {
            return unused;
        }
        auto compacted = compact_if_changed();
        if (!compacted) {
            return compacted;
        }
        return refresh_page_hints();
    }

    auto apply_redirects() -> base::Status {
        for (const auto redirect : redirects_) {
            if (candidates_.is_dead(redirect.root)) {
                continue;
            }
            auto status = page_.redirect(redirect.root, redirect.target);
            if (!status) {
                return status;
            }
            result_.redirects.push_back(redirect);
            ++result_.redirects_installed;
            ++result_.tuples_pruned;
        }
        return {};
    }

    auto apply_dead() -> base::Status {
        for (std::size_t index = 0; index < count_; ++index) {
            const auto slot = base::SlotId{static_cast<std::uint16_t>(index)};
            if (!candidates_.is_dead(slot)) {
                continue;
            }
            auto status = page_.write_line_pointer(slot, LinePointer::dead());
            if (!status) {
                return status;
            }
            result_.dead.push_back(slot);
            ++result_.tuples_pruned;
        }
        return {};
    }

    auto apply_unused() -> base::Status {
        for (std::size_t index = 0; index < count_; ++index) {
            const auto slot = base::SlotId{static_cast<std::uint16_t>(index)};
            if (!candidates_.is_unused(slot) || candidates_.is_dead(slot)) {
                continue;
            }
            auto status = apply_unused_slot(slot);
            if (!status) {
                return status;
            }
        }
        return {};
    }

    auto apply_unused_slot(const base::SlotId slot) -> base::Status {
        auto pointer = page_.line_pointer(slot);
        if (!pointer) {
            return base::fail(std::move(pointer).error());
        }
        if (pointer->state != LinePointerState::kNormal) {
            return {};
        }
        auto status = page_.write_line_pointer(slot, LinePointer::unused());
        if (!status) {
            return status;
        }
        result_.unused.push_back(slot);
        ++result_.tuples_pruned;
        return {};
    }

    auto compact_if_changed() -> base::Status {
        if (!result_.has_storage_changes()) {
            return {};
        }
        const auto before_count = page_.item_count();
        auto reclaimed = CompactionOperation{page_}.run();
        if (!reclaimed) {
            return base::fail(std::move(reclaimed).error());
        }
        result_.space_reclaimed = reclaimed.value();
        result_.line_pointers_truncated = before_count - page_.item_count();
        return {};
    }

    auto has_future_prune_candidate() const -> base::Result<bool> {
        for (std::size_t index = 0; index < page_.item_count(); ++index) {
            auto candidate =
                future_prune_candidate(base::SlotId{static_cast<std::uint16_t>(index)});
            if (!candidate) {
                return base::fail(std::move(candidate).error());
            }
            if (candidate.value()) {
                return true;
            }
        }
        return false;
    }

    auto future_prune_candidate(const base::SlotId slot) const -> base::Result<bool> {
        auto pointer = page_.line_pointer(slot);
        if (!pointer) {
            return base::fail(std::move(pointer).error());
        }
        if (pointer->state != LinePointerState::kNormal) {
            return false;
        }
        auto header = read_header(slot);
        if (!header) {
            return base::fail(std::move(header).error());
        }
        const auto removable_later = header->has_flag(tuple_flags::kHotUpdated)
                                     || header->has_flag(tuple_flags::kUpdated)
                                     || header->has_flag(tuple_flags::kDeleted);
        return removable_later && transaction::is_normal_xid(header->xmax)
               && !commit_log_.is_aborted(header->xmax);
    }

    auto refresh_page_hints() -> base::Status {
        auto remains_prunable = has_future_prune_candidate();
        if (!remains_prunable) {
            return base::fail(std::move(remains_prunable).error());
        }
        auto flags = page_.page_->flags();
        if (!flags) {
            return base::fail(std::move(flags).error());
        }
        auto new_flags = static_cast<std::uint16_t>(
            flags.value() & ~(heap_page_flags::kPrunable | heap_page_flags::kFull)
        );
        if (remains_prunable.value()) {
            new_flags |= heap_page_flags::kPrunable;
        }
        return page_.page_->set_flags(new_flags);
    }

    HeapPage& page_;
    const transaction::CommitLog& commit_log_;
    base::Xid oldest_xmin_;
    storage::Page before_;
    std::size_t count_;
    PruneCandidates candidates_;
    std::vector<HeapPruneRedirect> redirects_;
    HeapPruneResult result_;
};

[[maybe_unused]] auto HeapPage::repair_fragmentation() -> base::Result<std::size_t> {
    // LAB-S01.Q07:R14-BEGIN
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q07:R14: algorithm not implemented"}
    );
    // LAB-S01.Q07:R14-END
}

auto HeapPage::prune(const transaction::CommitLog& commit_log, const base::Xid oldest_xmin)
    -> base::Result<HeapPruneResult> {
    if (!valid_prune_horizon(oldest_xmin)) {
        return base::fail(prune_error(
            base::ErrorCode::kInvalidArgument,
            "prune heap page",
            "heap prune horizon must be a normal XID or the no-active-XID boundary"
        ));
    }
    auto valid = validate_layout();
    if (!valid) {
        return base::fail(std::move(valid).error());
    }
    return PruneOperation{*this, commit_log, oldest_xmin}.run();
}

}  // namespace db::access
