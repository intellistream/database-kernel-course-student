#include "db/storage/buffer_pool.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/exception_boundary.hpp"
#include "db/base/lab_missing.hpp"

namespace db::storage {
namespace {

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)};
}

[[maybe_unused]] auto unavailable(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, std::move(message)};
}

[[maybe_unused]] auto invariant(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvariantViolation, std::move(message)};
}

constexpr base::ExceptionBoundaryPolicy kBufferPoolExceptionPolicy{
    .component = "storage.buffer_pool",
    .allocation_message = "buffer pool exhausted available memory",
    .exception_prefix = "unexpected exception escaped buffer pool internals: ",
    .unknown_exception_message = "non-standard exception escaped buffer pool internals",
};

constexpr base::ExceptionBoundaryPolicy kDiskManagerExceptionPolicy{
    .component = "storage.buffer_pool",
    .allocation_message = "disk manager port exhausted available memory",
    .exception_prefix = "disk manager port threw an exception: ",
    .unknown_exception_message = "disk manager port threw a non-standard exception",
};

constexpr base::ExceptionBoundaryPolicy kDurabilityExceptionPolicy{
    .component = "storage.buffer_pool",
    .allocation_message = "page durability port exhausted available memory",
    .exception_prefix = "page durability port threw an exception: ",
    .unknown_exception_message = "page durability port threw a non-standard exception",
};

constexpr base::ExceptionBoundaryPolicy kReplacementExceptionPolicy{
    .component = "storage.buffer_pool",
    .allocation_message = "replacement policy exhausted available memory",
    .exception_prefix = "replacement policy threw an exception: ",
    .unknown_exception_message = "replacement policy threw a non-standard exception",
};

constexpr std::size_t kLocalMappingHintCount = 8U;

enum class PoolState : std::uint8_t {
    kOpen,
    kClosing,
    kShutdown,
};

class BufferMapping final {
    struct LoadWaitState final {
        std::atomic_bool completed{false};
    };

public:
    static constexpr std::size_t kPartitionCount = 128;

    class LoadingLease final {
    public:
        LoadingLease(BufferMapping& owner, const PageAddress address) noexcept
            : owner_{&owner},
              address_{address} {}

        LoadingLease(const LoadingLease&) = delete;
        auto operator=(const LoadingLease&) -> LoadingLease& = delete;

        LoadingLease(LoadingLease&& other) noexcept
            : owner_{std::exchange(other.owner_, nullptr)},
              address_{other.address_} {}

        auto operator=(LoadingLease&&) -> LoadingLease& = delete;

        ~LoadingLease() noexcept {
            if (owner_ != nullptr) {
                owner_->finish_loading(address_);
            }
        }

    private:
        BufferMapping* owner_;
        PageAddress address_;
    };

    explicit BufferMapping(const std::size_t capacity) {
        const auto partition_capacity = capacity / kPartitionCount + 1U;
        for (auto& partition : partitions_) {
            partition.pages.reserve(partition_capacity);
            partition.loading.reserve(partition_capacity);
        }
    }

    [[maybe_unused]] auto lookup([[maybe_unused]] const PageAddress address) const
        -> std::optional<base::FrameId> {
        // LAB-S01.Q04:R11-BEGIN
        static_cast<void>(address);
        base::lab::missing_algorithm("S01.Q04:R11");
        // LAB-S01.Q04:R11-END
    }

    [[maybe_unused]] auto begin_loading([[maybe_unused]] const PageAddress address)
        -> std::optional<LoadingLease> {
        // LAB-S01.Q04:R12-BEGIN
        static_cast<void>(address);
        base::lab::missing_algorithm("S01.Q04:R12");
        // LAB-S01.Q04:R12-END
    }

    [[maybe_unused]] auto insert(
        [[maybe_unused]] const PageAddress address,
        [[maybe_unused]] const base::FrameId frame_id
    ) -> bool {
        // LAB-S01.Q04:R13-BEGIN
        static_cast<void>(address);
        static_cast<void>(frame_id);
        base::lab::missing_algorithm("S01.Q04:R13");
        // LAB-S01.Q04:R13-END
    }

    [[maybe_unused]] auto remap(
        [[maybe_unused]] const PageAddress old_address,
        [[maybe_unused]] const PageAddress new_address,
        [[maybe_unused]] const base::FrameId frame_id
    ) -> bool {
        // LAB-S01.Q04:R14-BEGIN
        static_cast<void>(old_address);
        static_cast<void>(new_address);
        static_cast<void>(frame_id);
        base::lab::missing_algorithm("S01.Q04:R14");
        // LAB-S01.Q04:R14-END
    }

    [[maybe_unused]] auto erase_if(
        [[maybe_unused]] const PageAddress address,
        [[maybe_unused]] const base::FrameId expected_frame_id
    ) -> bool {
        // LAB-S01.Q04:R15-BEGIN
        static_cast<void>(address);
        static_cast<void>(expected_frame_id);
        base::lab::missing_algorithm("S01.Q04:R15");
        // LAB-S01.Q04:R15-END
    }

    [[maybe_unused]] auto
    relation_entries([[maybe_unused]] const base::RelationId relation_id) const
        -> std::vector<std::pair<PageAddress, base::FrameId>> {
        // LAB-S01.Q04:R16-BEGIN
        static_cast<void>(relation_id);
        base::lab::missing_algorithm("S01.Q04:R16");
        // LAB-S01.Q04:R16-END
    }

    [[maybe_unused]] auto addresses() const -> std::vector<PageAddress> {
        // LAB-S01.Q04:R17-BEGIN
        base::lab::missing_algorithm("S01.Q04:R17");
        // LAB-S01.Q04:R17-END
    }

private:
    struct LoadingEntry final {
        std::shared_ptr<LoadWaitState> waiters;
    };

    struct Partition final {
        mutable std::shared_mutex mutex;
        std::unordered_map<PageAddress, base::FrameId> pages;
        std::unordered_map<PageAddress, LoadingEntry> loading;
    };

    [[maybe_unused]] void finish_loading(const PageAddress address) noexcept {
        auto& partition = partition_for(address);
        std::shared_ptr<LoadWaitState> waiters;
        {
            const std::unique_lock lock{partition.mutex};
            const auto loading = partition.loading.find(address);
            if (loading == partition.loading.end()) {
                std::terminate();
            }
            waiters = std::move(loading->second.waiters);
            partition.loading.erase(loading);
        }
        if (waiters) {
            waiters->completed.store(true, std::memory_order_release);
            waiters->completed.notify_all();
        }
    }

    [[maybe_unused]] static auto
    partition_index([[maybe_unused]] const PageAddress address) noexcept -> std::size_t {
        // LAB-S01.Q04:R18-BEGIN
        static_cast<void>(address);
        base::lab::missing_algorithm("S01.Q04:R18");
        // LAB-S01.Q04:R18-END
    }

    [[maybe_unused]] auto partition_by_index(const std::size_t index) noexcept -> Partition& {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) -- masked index.
        return partitions_[index];
    }

    [[maybe_unused]] auto partition_by_index(const std::size_t index) const noexcept
        -> const Partition& {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) -- masked index.
        return partitions_[index];
    }

    [[maybe_unused]] auto partition_for(const PageAddress address) noexcept -> Partition& {
        return partition_by_index(partition_index(address));
    }

    [[maybe_unused]] auto partition_for(const PageAddress address) const noexcept
        -> const Partition& {
        return partition_by_index(partition_index(address));
    }

    static_assert((kPartitionCount & (kPartitionCount - 1U)) == 0U);
    std::array<Partition, kPartitionCount> partitions_;
};

// Owns one successful Frame::begin_io transition. Explicit completion reports
// state errors; destruction only prevents exceptional unwinding from leaving
// waiters blocked behind io_in_progress.
class FrameIoScope final {
public:
    FrameIoScope(Frame& frame, PinGuard& pin) noexcept
        : frame_{&frame},
          pin_{&pin} {}

    FrameIoScope(const FrameIoScope&) = delete;
    auto operator=(const FrameIoScope&) -> FrameIoScope& = delete;
    FrameIoScope(FrameIoScope&&) = delete;
    auto operator=(FrameIoScope&&) -> FrameIoScope& = delete;

    ~FrameIoScope() noexcept {
        if (frame_ == nullptr) {
            return;
        }
        try {
            static_cast<void>(finish(false));
        } catch (...) {  // NOLINT(bugprone-empty-catch) -- latch cleanup cannot report failure.
            // No exception may escape cleanup during stack unwinding.
        }
    }

    [[maybe_unused]] auto finish(const bool succeeded) -> base::Status {
        auto* const frame = std::exchange(frame_, nullptr);
        if (frame == nullptr || pin_ == nullptr) {
            return base::fail(invariant("frame I/O scope has already been completed"));
        }
        return frame->finish_io(*pin_, succeeded);
    }

    [[maybe_unused]] auto
    finish_failure(base::Error primary_error, const std::string_view operation) -> base::Error {
        auto finished = finish(false);
        if (!finished) {
            return std::move(finished).error().with_context(
                "storage.buffer_pool",
                std::string{operation}
                    + "; original writeback failure: " + primary_error.to_string()
            );
        }
        return primary_error;
    }

private:
    Frame* frame_;
    PinGuard* pin_;
};

}  // namespace

struct BufferPool::Impl final {
    Impl(
        const std::size_t pool_size,
        std::unique_ptr<DiskManager> disk_manager,
        std::unique_ptr<ReplacementPolicy> replacement_policy,
        std::unique_ptr<PageDurabilityPort> durability_port
    )
        : disk{std::move(disk_manager)},
          replacement{std::move(replacement_policy)},
          durability{std::move(durability_port)},
          mapping{pool_size},
          free_frame_count{pool_size} {
        frames.reserve(pool_size);
        frame_view.reserve(pool_size);
        free_frames.reserve(pool_size);
        for (std::size_t index = 0; index < pool_size; ++index) {
            frames.push_back(
                std::make_unique<Frame>(base::FrameId{
                    static_cast<std::uint32_t>(index),
                })
            );
            frame_view.push_back(frames.back().get());
            free_frames.emplace_back(static_cast<std::uint32_t>(pool_size - index - 1U));
        }
    }

    auto fetch(PageAddress address) -> base::Result<PinGuard>;
    auto try_cache_hit(PageAddress address) -> std::optional<PinGuard>;
    auto lookup(PageAddress address) const -> std::optional<base::FrameId>;
    auto reserve_free_frame() -> std::optional<base::FrameId>;
    auto reserve_free_frame_exact() -> std::optional<base::FrameId>;
    void release_free_frame(base::FrameId frame_id) noexcept;
    auto install_miss(PageAddress address, const Page& page, base::FrameId frame_id)
        -> base::Result<PinGuard>;
    auto replace_miss(PageAddress address, const Page& page) -> base::Result<PinGuard>;
    auto writeback_image(PinGuard& pin, std::optional<base::Lsn> checkpoint_max_lsn)
        -> base::Result<bool>;
    auto writeback(PinGuard& pin) -> base::Status;
    auto flush(PageAddress address) -> base::Result<bool>;
    auto flush_everything() -> base::Status;
    auto mark_checkpoint_dirty() -> base::Result<CheckpointDirtySnapshot>;
    auto flush_checkpoint_batch(
        std::size_t& cursor,
        std::size_t frame_budget,
        base::Lsn snapshot_max_lsn
    ) -> base::Result<std::size_t>;
    auto any_pinned() const -> bool;

    auto accepts_operations() const noexcept -> bool {
        return state.load(std::memory_order_acquire) == PoolState::kOpen;
    }

    std::unique_ptr<DiskManager> disk;
    std::unique_ptr<ReplacementPolicy> replacement;
    std::unique_ptr<PageDurabilityPort> durability;
    std::vector<std::unique_ptr<Frame>> frames;
    std::vector<Frame*> frame_view;

    std::atomic<PoolState> state{PoolState::kOpen};
    std::mutex lifecycle_mutex;
    BufferMapping mapping;
    std::atomic_size_t free_frame_count;
    std::mutex free_frames_mutex;
    std::vector<base::FrameId> free_frames;
};

auto BufferPool::Impl::lookup(const PageAddress address) const -> std::optional<base::FrameId> {
    return mapping.lookup(address);
}

[[maybe_unused]] auto BufferPool::Impl::reserve_free_frame() -> std::optional<base::FrameId> {
    // LAB-S01.Q04:R19-BEGIN
    base::lab::missing_algorithm("S01.Q04:R19");
    // LAB-S01.Q04:R19-END
}

[[maybe_unused]] auto BufferPool::Impl::reserve_free_frame_exact() -> std::optional<base::FrameId> {
    // LAB-S01.Q04:R20-BEGIN
    base::lab::missing_algorithm("S01.Q04:R20");
    // LAB-S01.Q04:R20-END
}

[[maybe_unused]] void
BufferPool::Impl::release_free_frame([[maybe_unused]] const base::FrameId frame_id) noexcept {
    // LAB-S01.Q04:R21-BEGIN
    static_cast<void>(frame_id);
    base::lab::missing_algorithm("S01.Q04:R21");
    // LAB-S01.Q04:R21-END
}

[[maybe_unused]] auto BufferPool::Impl::fetch([[maybe_unused]] const PageAddress address)
    -> base::Result<PinGuard> {
    // LAB-S01.Q04:R22-BEGIN
    static_cast<void>(address);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R22: algorithm not implemented"}
    );
    // LAB-S01.Q04:R22-END
}

[[maybe_unused]] auto BufferPool::Impl::try_cache_hit([[maybe_unused]] const PageAddress address)
    -> std::optional<PinGuard> {
    // LAB-S01.Q04:R23-BEGIN
    static_cast<void>(address);
    base::lab::missing_algorithm("S01.Q04:R23");
    // LAB-S01.Q04:R23-END
}

[[maybe_unused]] auto BufferPool::Impl::install_miss(
    [[maybe_unused]] const PageAddress address,
    [[maybe_unused]] const Page& page,
    [[maybe_unused]] const base::FrameId frame_id
) -> base::Result<PinGuard> {
    // LAB-S01.Q04:R24-BEGIN
    static_cast<void>(address);
    static_cast<void>(page);
    static_cast<void>(frame_id);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R24: algorithm not implemented"}
    );
    // LAB-S01.Q04:R24-END
}

[[maybe_unused]] auto BufferPool::Impl::writeback_image(
    [[maybe_unused]] PinGuard& pin,
    [[maybe_unused]] const std::optional<base::Lsn> checkpoint_max_lsn
) -> base::Result<bool> {
    // LAB-S01.Q05:R07-BEGIN
    static_cast<void>(pin);
    static_cast<void>(checkpoint_max_lsn);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R07: algorithm not implemented"}
    );
    // LAB-S01.Q05:R07-END
}

[[maybe_unused]] auto BufferPool::Impl::writeback([[maybe_unused]] PinGuard& pin) -> base::Status {
    // LAB-S01.Q05:R08-BEGIN
    static_cast<void>(pin);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R08: algorithm not implemented"}
    );
    // LAB-S01.Q05:R08-END
}

[[maybe_unused]] auto BufferPool::Impl::replace_miss(
    [[maybe_unused]] const PageAddress address,
    [[maybe_unused]] const Page& page
) -> base::Result<PinGuard> {
    // LAB-S01.Q04:R25-BEGIN
    static_cast<void>(address);
    static_cast<void>(page);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R25: algorithm not implemented"}
    );
    // LAB-S01.Q04:R25-END
}

[[maybe_unused]] auto BufferPool::Impl::flush([[maybe_unused]] const PageAddress address)
    -> base::Result<bool> {
    // LAB-S01.Q05:R09-BEGIN
    static_cast<void>(address);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R09: algorithm not implemented"}
    );
    // LAB-S01.Q05:R09-END
}

[[maybe_unused]] auto BufferPool::Impl::flush_everything() -> base::Status {
    // LAB-S01.Q05:R10-BEGIN
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R10: algorithm not implemented"}
    );
    // LAB-S01.Q05:R10-END
}

[[maybe_unused]] auto BufferPool::Impl::mark_checkpoint_dirty()
    -> base::Result<CheckpointDirtySnapshot> {
    // LAB-S01.Q05:R11-BEGIN
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R11: algorithm not implemented"}
    );
    // LAB-S01.Q05:R11-END
}

[[maybe_unused]] auto BufferPool::Impl::flush_checkpoint_batch(
    [[maybe_unused]] std::size_t& cursor,
    [[maybe_unused]] const std::size_t frame_budget,
    [[maybe_unused]] const base::Lsn snapshot_max_lsn
) -> base::Result<std::size_t> {
    // LAB-S01.Q05:R12-BEGIN
    static_cast<void>(cursor);
    static_cast<void>(frame_budget);
    static_cast<void>(snapshot_max_lsn);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R12: algorithm not implemented"}
    );
    // LAB-S01.Q05:R12-END
}

[[maybe_unused]] auto BufferPool::Impl::any_pinned() const -> bool {
    // LAB-S01.Q04:R26-BEGIN
    base::lab::missing_algorithm("S01.Q04:R26");
    // LAB-S01.Q04:R26-END
}

auto BufferPool::create(
    const std::size_t pool_size,
    std::unique_ptr<DiskManager> disk,
    std::unique_ptr<ReplacementPolicy> replacement,
    std::unique_ptr<PageDurabilityPort> durability
) -> base::Result<std::unique_ptr<BufferPool>> {
    return base::invoke_exception_boundary(
        kBufferPoolExceptionPolicy,
        "create buffer pool",
        [&]() -> base::Result<std::unique_ptr<BufferPool>> {
            if (pool_size == 0) {
                return base::fail(invalid("buffer pool size must be greater than zero"));
            }
            if (pool_size > std::numeric_limits<std::uint32_t>::max()) {
                return base::fail(invalid("buffer pool size exceeds the FrameId domain"));
            }
            if (!disk) {
                return base::fail(invalid("buffer pool requires a disk manager"));
            }
            if (!replacement) {
                auto clock = ClockReplacer::create(pool_size);
                if (!clock) {
                    return base::fail(std::move(clock).error());
                }
                replacement = std::move(clock).value();
            }
            return std::unique_ptr<BufferPool>{new BufferPool{std::make_unique<
                Impl>(pool_size, std::move(disk), std::move(replacement), std::move(durability))}};
        }
    );
}

BufferPool::BufferPool(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

BufferPool::~BufferPool() = default;

auto BufferPool::pool_size() const noexcept -> std::size_t {
    return implementation_->frames.size();
}

auto BufferPool::is_shutdown() const noexcept -> bool {
    return implementation_->state.load(std::memory_order_acquire) == PoolState::kShutdown;
}

[[maybe_unused]] auto BufferPool::pin_page([[maybe_unused]] const PageAddress address)
    -> base::Result<PinGuard> {
    // LAB-S01.Q04:R01-BEGIN
    static_cast<void>(address);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R01: algorithm not implemented"}
    );
    // LAB-S01.Q04:R01-END
}

[[maybe_unused]] auto BufferPool::read_page([[maybe_unused]] const PageAddress address)
    -> base::Result<PageReadGuard> {
    // LAB-S01.Q04:R02-BEGIN
    static_cast<void>(address);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R02: algorithm not implemented"}
    );
    // LAB-S01.Q04:R02-END
}

[[maybe_unused]] auto BufferPool::write_page([[maybe_unused]] const PageAddress address)
    -> base::Result<PageWriteGuard> {
    // LAB-S01.Q04:R03-BEGIN
    static_cast<void>(address);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R03: algorithm not implemented"}
    );
    // LAB-S01.Q04:R03-END
}

[[maybe_unused]] auto BufferPool::try_write_page([[maybe_unused]] const PageAddress address)
    -> base::Result<PageWriteGuard> {
    // LAB-S01.Q04:R04-BEGIN
    static_cast<void>(address);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R04: algorithm not implemented"}
    );
    // LAB-S01.Q04:R04-END
}

[[maybe_unused]] auto BufferPool::allocate_page(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const PageKind kind,
    [[maybe_unused]] const std::size_t special_size
) -> base::Result<PinGuard> {
    // LAB-S01.Q04:R05-BEGIN
    static_cast<void>(relation_id);
    static_cast<void>(kind);
    static_cast<void>(special_size);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R05: algorithm not implemented"}
    );
    // LAB-S01.Q04:R05-END
}

[[maybe_unused]] auto
BufferPool::relation_page_count([[maybe_unused]] const base::RelationId relation_id)
    -> base::Result<std::uint64_t> {
    // LAB-S01.Q04:R06-BEGIN
    static_cast<void>(relation_id);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R06: algorithm not implemented"}
    );
    // LAB-S01.Q04:R06-END
}

[[maybe_unused]] auto BufferPool::sync_relation([[maybe_unused]] const base::RelationId relation_id)
    -> base::Status {
    // LAB-S01.Q05:R01-BEGIN
    static_cast<void>(relation_id);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R01: algorithm not implemented"}
    );
    // LAB-S01.Q05:R01-END
}

[[maybe_unused]] auto BufferPool::remove_relation(
    [[maybe_unused]] const base::RelationId relation_id,
    [[maybe_unused]] const bool missing_ok
) -> base::Status {
    // LAB-S01.Q04:R07-BEGIN
    static_cast<void>(relation_id);
    static_cast<void>(missing_ok);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R07: algorithm not implemented"}
    );
    // LAB-S01.Q04:R07-END
}

[[maybe_unused]] auto BufferPool::flush_page([[maybe_unused]] const PageAddress address)
    -> base::Result<bool> {
    // LAB-S01.Q05:R02-BEGIN
    static_cast<void>(address);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R02: algorithm not implemented"}
    );
    // LAB-S01.Q05:R02-END
}

[[maybe_unused]] auto BufferPool::flush_all() -> base::Status {
    // LAB-S01.Q05:R03-BEGIN
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R03: algorithm not implemented"}
    );
    // LAB-S01.Q05:R03-END
}

[[maybe_unused]] auto BufferPool::sync_all() -> base::Status {
    // LAB-S01.Q05:R04-BEGIN
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R04: algorithm not implemented"}
    );
    // LAB-S01.Q05:R04-END
}

[[maybe_unused]] auto BufferPool::mark_checkpoint_dirty_pages()
    -> base::Result<CheckpointDirtySnapshot> {
    // LAB-S01.Q05:R05-BEGIN
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R05: algorithm not implemented"}
    );
    // LAB-S01.Q05:R05-END
}

[[maybe_unused]] auto BufferPool::flush_checkpoint_dirty_batch(
    [[maybe_unused]] std::size_t& cursor,
    [[maybe_unused]] const std::size_t frame_budget,
    [[maybe_unused]] const base::Lsn snapshot_max_lsn
) -> base::Result<std::size_t> {
    // LAB-S01.Q05:R06-BEGIN
    static_cast<void>(cursor);
    static_cast<void>(frame_budget);
    static_cast<void>(snapshot_max_lsn);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R06: algorithm not implemented"}
    );
    // LAB-S01.Q05:R06-END
}

[[maybe_unused]] auto BufferPool::shutdown() -> base::Status {
    // LAB-S01.Q04:R08-BEGIN
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R08: algorithm not implemented"}
    );
    // LAB-S01.Q04:R08-END
}

[[maybe_unused]] auto BufferPool::contains([[maybe_unused]] const PageAddress address) const
    -> bool {
    // LAB-S01.Q04:R09-BEGIN
    static_cast<void>(address);
    base::lab::missing_algorithm("S01.Q04:R09");
    // LAB-S01.Q04:R09-END
}

[[maybe_unused]] auto BufferPool::frame_snapshot([[maybe_unused]] const PageAddress address) const
    -> base::Result<FrameSnapshot> {
    // LAB-S01.Q04:R10-BEGIN
    static_cast<void>(address);
    return base::fail(
        base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R10: algorithm not implemented"}
    );
    // LAB-S01.Q04:R10-END
}

}  // namespace db::storage
