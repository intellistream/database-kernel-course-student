#include "db/recovery/recovery_page_cache.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::recovery {
namespace {

[[maybe_unused]] auto
cache_error(const base::ErrorCode code, const std::string_view operation, std::string message)
    -> base::Error {
    return base::Error{code, std::move(message)}.with_context(
        "recovery.page_cache",
        std::string{operation}
    );
}

[[maybe_unused]] auto valid_page_kind(const storage::PageKind kind) noexcept -> bool {
    switch (kind) {
        case storage::PageKind::kHeap:
        case storage::PageKind::kBtree:
        case storage::PageKind::kFreeSpaceMap:
        case storage::PageKind::kMetadata:
            return true;
        case storage::PageKind::kUninitialized:
            return false;
    }
    return false;
}

}  // namespace

struct RecoveryPageCache::Impl final {
    enum class State : std::uint8_t {
        kOpen,
        kDraining,
        kFinalized,
        kAbandoned,
    };

    struct Entry final {
        storage::PageAddress address;
        storage::PageKind kind;
        storage::Page page;
        bool dirty{false};
        bool referenced{true};
        std::size_t pin_count{0U};
    };

    struct SlotSelection final {
        std::size_t slot{0U};
        bool replaces_entry{false};
    };

    class Lease final : public StartupPageLease {
    public:
        Lease(Impl& owner, const std::size_t slot, const bool discard_clean_new_entry) noexcept
            : owner_{&owner},
              slot_{slot},
              discard_clean_new_entry_{discard_clean_new_entry} {}

        Lease(const Lease&) = delete;
        auto operator=(const Lease&) -> Lease& = delete;
        Lease(Lease&&) = delete;
        auto operator=(Lease&&) -> Lease& = delete;

        ~Lease() override {
            if (owner_ != nullptr) {
                owner_->release_lease(slot_, discard_clean_new_entry_);
            }
        }

        auto page() noexcept -> storage::Page& override {
            return owner_->entry_at(slot_).page;
        }

        [[maybe_unused]] auto mark_dirty() -> base::Status override {
            // LAB-S10.Q09:R25-BEGIN
            return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R25"});
            // LAB-S10.Q09:R25-END
        }

    private:
        Impl* owner_;
        std::size_t slot_;
        bool discard_clean_new_entry_;
    };

    [[maybe_unused]] Impl(
        [[maybe_unused]] storage::DiskManager& disk_manager,
        [[maybe_unused]] storage::PageDurabilityPort& durability_port,
        [[maybe_unused]] const std::size_t capacity
    )
        : disk_{disk_manager},
          durability_{durability_port},
          slots_(capacity) {
        // LAB-S10.Q09:R26-BEGIN
        base::lab::missing_algorithm("S10.Q09:R26");
        // LAB-S10.Q09:R26-END
    }

    auto acquire(const StartupPageRequest& request)
        -> base::Result<std::unique_ptr<StartupPageLease>>;
    auto drain_and_sync() -> base::Status;
    auto abandon_for_retry() -> base::Status;
    auto stats_snapshot() const noexcept -> RecoveryPageCacheStats;
    auto cached_page_count() const noexcept -> std::size_t;
    auto contains(storage::PageAddress address) const noexcept -> bool;
    auto is_finalized() const noexcept -> bool;
    auto is_abandoned() const noexcept -> bool;
    auto safe_to_destroy() const noexcept -> bool;

private:
    auto validate_request(const StartupPageRequest& request) const -> base::Status;
    auto relation_page_count(base::RelationId relation_id) -> base::Result<std::uint64_t>;
    auto load_or_create_page(const StartupPageRequest& request) -> base::Result<storage::Page>;
    auto select_slot() -> base::Result<SlotSelection>;
    auto install_entry(
        SlotSelection selection,
        const storage::Page& page,
        const StartupPageRequest& request
    ) -> base::Result<std::size_t>;
    auto writeback_entry(Entry& entry) -> base::Status;
    auto entry_at(std::size_t slot) noexcept -> Entry&;
    auto entry_at(std::size_t slot) const noexcept -> const Entry&;
    auto mark_dirty(std::size_t slot) -> base::Status;
    void release_lease(std::size_t slot, bool discard_clean_new_entry) noexcept;
    void remove_clean_entry(std::size_t slot) noexcept;
    void advance_clock() noexcept;

    storage::DiskManager& disk_;
    storage::PageDurabilityPort& durability_;
    std::vector<std::optional<Entry>> slots_;
    std::vector<std::size_t> free_slots_;
    std::unordered_map<storage::PageAddress, std::size_t> slot_by_address_;
    std::unordered_map<base::RelationId, std::uint64_t> relation_page_counts_;
    std::set<base::RelationId> relations_pending_sync_;
    RecoveryPageCacheStats stats_;
    std::size_t clock_hand_{0U};
    std::size_t active_leases_{0U};
    State state_{State::kOpen};
};

auto RecoveryPageCache::Impl::validate_request(const StartupPageRequest& request) const
    -> base::Status {
    if (!request.address.valid()) {
        return base::fail(cache_error(
            base::ErrorCode::kInvalidArgument,
            "acquire page",
            "startup page request has an invalid relation or page identifier"
        ));
    }
    if (!valid_page_kind(request.expected_kind)) {
        return base::fail(cache_error(
            base::ErrorCode::kInvalidArgument,
            "acquire page",
            "startup page request has an uninitialized page kind"
        ));
    }
    return {};
}

[[maybe_unused]] auto
RecoveryPageCache::Impl::relation_page_count([[maybe_unused]] const base::RelationId relation_id)
    -> base::Result<std::uint64_t> {
    // LAB-S10.Q09:R27-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R27"});
    // LAB-S10.Q09:R27-END
}

auto RecoveryPageCache::Impl::load_or_create_page(const StartupPageRequest& request)
    -> base::Result<storage::Page> {
    auto discovered_page_count = relation_page_count(request.address.relation_id);
    if (!discovered_page_count) {
        return base::fail(std::move(discovered_page_count).error());
    }
    auto page_count = discovered_page_count.value();
    const auto requested_page = static_cast<std::uint64_t>(request.address.page_id.value());
    while (page_count <= requested_page) {
        auto allocated = disk_.allocate_page(request.address.relation_id, request.expected_kind);
        if (!allocated) {
            return base::fail(
                std::move(allocated).error().with_context(
                    "recovery.page_cache",
                    "allocate recovery page"
                )
            );
        }
        const storage::PageAddress expected_address{
            request.address.relation_id,
            base::PageId{static_cast<std::uint32_t>(page_count)},
        };
        if (allocated.value() != expected_address) {
            return base::fail(cache_error(
                base::ErrorCode::kInvariantViolation,
                "acquire page",
                "disk allocation did not extend the recovery relation at its next page"
            ));
        }
        ++page_count;
        relation_page_counts_[request.address.relation_id] = page_count;
        relations_pending_sync_.insert(request.address.relation_id);
        ++stats_.allocated_pages;
    }

    if (request.load_mode == StartupPageLoadMode::kSkipExisting) {
        auto empty = storage::Page::create(request.expected_kind, request.address);
        if (!empty) {
            return base::fail(
                std::move(empty).error().with_context(
                    "recovery.page_cache",
                    "create replacement page"
                )
            );
        }
        return std::move(empty).value();
    }

    auto loaded = disk_.read_page(request.address);
    if (!loaded) {
        return base::fail(
            std::move(loaded).error().with_context("recovery.page_cache", "read recovery page")
        );
    }
    auto header = loaded->header();
    if (!header) {
        return base::fail(
            std::move(header).error().with_context("recovery.page_cache", "inspect page kind")
        );
    }
    if (header->kind != request.expected_kind) {
        return base::fail(cache_error(
            base::ErrorCode::kCorruption,
            "acquire page",
            "durable page kind disagrees with the validated recovery route"
        ));
    }
    ++stats_.page_reads;
    return std::move(loaded).value();
}

[[maybe_unused]] auto RecoveryPageCache::Impl::select_slot()
    -> base::Result<RecoveryPageCache::Impl::SlotSelection> {
    // LAB-S10.Q09:R28-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R28"});
    // LAB-S10.Q09:R28-END
}

[[maybe_unused]] auto RecoveryPageCache::Impl::install_entry(
    [[maybe_unused]] const SlotSelection selection,
    [[maybe_unused]] const storage::Page& page,
    [[maybe_unused]] const StartupPageRequest& request
) -> base::Result<std::size_t> {
    // LAB-S10.Q09:R29-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R29"});
    // LAB-S10.Q09:R29-END
}

[[maybe_unused]] auto RecoveryPageCache::Impl::writeback_entry([[maybe_unused]] Entry& entry)
    -> base::Status {
    // LAB-S10.Q09:R30-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R30"});
    // LAB-S10.Q09:R30-END
}

[[maybe_unused]] auto
RecoveryPageCache::Impl::entry_at([[maybe_unused]] const std::size_t slot) noexcept
    -> RecoveryPageCache::Impl::Entry& {
    // LAB-S10.Q09:R31-BEGIN
    base::lab::missing_algorithm("S10.Q09:R31");
    // LAB-S10.Q09:R31-END
}

[[maybe_unused]] auto
RecoveryPageCache::Impl::entry_at([[maybe_unused]] const std::size_t slot) const noexcept
    -> const RecoveryPageCache::Impl::Entry& {
    // LAB-S10.Q09:R32-BEGIN
    base::lab::missing_algorithm("S10.Q09:R32");
    // LAB-S10.Q09:R32-END
}

[[maybe_unused]] auto RecoveryPageCache::Impl::mark_dirty([[maybe_unused]] const std::size_t slot)
    -> base::Status {
    // LAB-S10.Q09:R33-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R33"});
    // LAB-S10.Q09:R33-END
}

[[maybe_unused]] void
RecoveryPageCache::Impl::remove_clean_entry([[maybe_unused]] const std::size_t slot) noexcept {
    // LAB-S10.Q09:R34-BEGIN
    base::lab::missing_algorithm("S10.Q09:R34");
    // LAB-S10.Q09:R34-END
}

[[maybe_unused]] void RecoveryPageCache::Impl::release_lease(
    [[maybe_unused]] const std::size_t slot,
    [[maybe_unused]] const bool discard_clean_new_entry
) noexcept {
    // LAB-S10.Q09:R35-BEGIN
    base::lab::missing_algorithm("S10.Q09:R35");
    // LAB-S10.Q09:R35-END
}

[[maybe_unused]] void RecoveryPageCache::Impl::advance_clock() noexcept {
    // LAB-S10.Q09:R36-BEGIN
    base::lab::missing_algorithm("S10.Q09:R36");
    // LAB-S10.Q09:R36-END
}

[[maybe_unused]] auto
RecoveryPageCache::Impl::acquire([[maybe_unused]] const StartupPageRequest& request)
    -> base::Result<std::unique_ptr<StartupPageLease>> {
    // LAB-S10.Q09:R37-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R37"});
    // LAB-S10.Q09:R37-END
}

[[maybe_unused]] auto RecoveryPageCache::Impl::drain_and_sync() -> base::Status {
    // LAB-S10.Q09:R38-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R38"});
    // LAB-S10.Q09:R38-END
}

[[maybe_unused]] auto RecoveryPageCache::Impl::abandon_for_retry() -> base::Status {
    // LAB-S10.Q09:R39-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R39"});
    // LAB-S10.Q09:R39-END
}

auto RecoveryPageCache::Impl::stats_snapshot() const noexcept -> RecoveryPageCacheStats {
    return stats_;
}

auto RecoveryPageCache::Impl::cached_page_count() const noexcept -> std::size_t {
    return slot_by_address_.size();
}

auto RecoveryPageCache::Impl::contains(storage::PageAddress address) const noexcept -> bool {
    return slot_by_address_.contains(address);
}

auto RecoveryPageCache::Impl::is_finalized() const noexcept -> bool {
    return state_ == State::kFinalized;
}

auto RecoveryPageCache::Impl::is_abandoned() const noexcept -> bool {
    return state_ == State::kAbandoned;
}

auto RecoveryPageCache::Impl::safe_to_destroy() const noexcept -> bool {
    if (active_leases_ != 0U || state_ == State::kDraining) {
        return false;
    }
    if (state_ == State::kFinalized || state_ == State::kAbandoned) {
        return true;
    }
    if (!relations_pending_sync_.empty()) {
        return false;
    }
    return std::none_of(slots_.begin(), slots_.end(), [](const auto& entry) {
        return entry.has_value() && entry->dirty;
    });
}

[[maybe_unused]] auto RecoveryPageCache::create(
    [[maybe_unused]] storage::DiskManager& disk,
    [[maybe_unused]] storage::PageDurabilityPort& durability,
    [[maybe_unused]] const RecoveryPageCacheOptions options
) -> base::Result<std::unique_ptr<RecoveryPageCache>> {
    // LAB-S10.Q09:R40-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R40"});
    // LAB-S10.Q09:R40-END
}

RecoveryPageCache::RecoveryPageCache(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

RecoveryPageCache::~RecoveryPageCache() {
    if (implementation_ != nullptr && !implementation_->safe_to_destroy()) {
        std::terminate();
    }
}

[[maybe_unused]] auto RecoveryPageCache::acquire([[maybe_unused]] const StartupPageRequest request)
    -> base::Result<std::unique_ptr<StartupPageLease>> {
    // LAB-S10.Q09:R41-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R41"});
    // LAB-S10.Q09:R41-END
}

[[maybe_unused]] auto RecoveryPageCache::drain_and_sync() -> base::Status {
    // LAB-S10.Q09:R42-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R42"});
    // LAB-S10.Q09:R42-END
}

[[maybe_unused]] auto RecoveryPageCache::abandon_for_retry() -> base::Status {
    // LAB-S10.Q09:R43-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S10.Q09:R43"});
    // LAB-S10.Q09:R43-END
}

auto RecoveryPageCache::stats() const noexcept -> RecoveryPageCacheStats {
    return implementation_->stats_snapshot();
}

auto RecoveryPageCache::cached_page_count() const noexcept -> std::size_t {
    return implementation_->cached_page_count();
}

auto RecoveryPageCache::contains(storage::PageAddress address) const noexcept -> bool {
    return implementation_->contains(address);
}

auto RecoveryPageCache::is_finalized() const noexcept -> bool {
    return implementation_->is_finalized();
}

auto RecoveryPageCache::is_abandoned() const noexcept -> bool {
    return implementation_->is_abandoned();
}

}  // namespace db::recovery
