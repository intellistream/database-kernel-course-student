#include "db/storage/replacement.hpp"

#include <atomic>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::storage {
namespace {

[[maybe_unused]] auto invalid(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvalidArgument, std::move(message)};
}

[[maybe_unused]] auto unavailable() -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, "no evictable frame is available"};
}

[[maybe_unused]] auto missing_algorithm(std::string_view region_id) -> base::Error {
    return base::Error{
        base::ErrorCode::kUnsupported,
        std::string{region_id} + ": algorithm not implemented",
    };
}

[[maybe_unused]] auto frame_index(const base::FrameId frame_id, const std::size_t pool_size)
    -> base::Result<std::size_t> {
    const auto index = static_cast<std::size_t>(frame_id.value());
    if (index >= pool_size) {
        return base::fail(invalid("frame id is outside the replacement policy's pool"));
    }
    return index;
}

struct LruBucket final {
    explicit LruBucket(const std::size_t pool_size)
        : previous(pool_size),
          next(pool_size) {}

    [[maybe_unused]] auto contains([[maybe_unused]] const std::size_t frame) const noexcept
        -> bool {
        // LAB-S01.Q03:R01-BEGIN
        static_cast<void>(frame);
        base::lab::missing_algorithm("S01.Q03:R01");
        // LAB-S01.Q03:R01-END
    }

    [[maybe_unused]] void unlink([[maybe_unused]] const std::size_t frame) noexcept {
        // LAB-S01.Q03:R02-BEGIN
        static_cast<void>(frame);
        base::lab::missing_algorithm("S01.Q03:R02");
        // LAB-S01.Q03:R02-END
    }

    [[maybe_unused]] void push_front([[maybe_unused]] const std::size_t frame) noexcept {
        // LAB-S01.Q03:R03-BEGIN
        static_cast<void>(frame);
        base::lab::missing_algorithm("S01.Q03:R03");
        // LAB-S01.Q03:R03-END
    }

    std::mutex mutex;
    std::vector<std::optional<std::size_t>> previous;
    std::vector<std::optional<std::size_t>> next;
    std::optional<std::size_t> head;
    std::optional<std::size_t> tail;
};

}  // namespace

void ReplacementPolicy::on_remove(const base::FrameId frame_id) noexcept {
    static_cast<void>(frame_id);
}

void ReplacementPolicy::on_restore(const base::FrameId frame_id) noexcept {
    on_pin(frame_id);
}

struct LruReplacer::Impl final {
    Impl(const std::size_t pool_size_value, const std::size_t bucket_count_value)
        : pool_size{pool_size_value} {
        buckets.reserve(bucket_count_value);
        for (std::size_t index = 0; index < bucket_count_value; ++index) {
            buckets.push_back(std::make_unique<LruBucket>(pool_size));
        }
    }

    std::size_t pool_size;
    std::vector<std::unique_ptr<LruBucket>> buckets;
    std::atomic<std::size_t> next_bucket{0};
};

auto LruReplacer::create(const std::size_t pool_size, const std::size_t bucket_count)
    -> base::Result<std::unique_ptr<LruReplacer>> {
    if (pool_size == 0) {
        return base::fail(invalid("LRU replacement requires a non-empty pool"));
    }
    if (bucket_count == 0 || bucket_count > pool_size) {
        return base::fail(invalid("LRU bucket count must be between one and the pool size"));
    }
    if (pool_size > std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(invalid("replacement pool exceeds the FrameId domain"));
    }
    return std::unique_ptr<LruReplacer>{
        new LruReplacer{std::make_unique<Impl>(pool_size, bucket_count)}
    };
}

LruReplacer::LruReplacer(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

LruReplacer::~LruReplacer() = default;

[[maybe_unused]] auto LruReplacer::evict([[maybe_unused]] const std::span<Frame* const> frames)
    -> base::Result<PinGuard> {
    // LAB-S01.Q03:R04-BEGIN
    static_cast<void>(frames);
    return base::fail(missing_algorithm("S01.Q03:R04"));
    // LAB-S01.Q03:R04-END
}

[[maybe_unused]] void LruReplacer::on_pin([[maybe_unused]] const base::FrameId frame_id) noexcept {
    // LAB-S01.Q03:R05-BEGIN
    static_cast<void>(frame_id);
    base::lab::missing_algorithm("S01.Q03:R05");
    // LAB-S01.Q03:R05-END
}

[[maybe_unused]] void
LruReplacer::on_remove([[maybe_unused]] const base::FrameId frame_id) noexcept {
    // LAB-S01.Q03:R06-BEGIN
    static_cast<void>(frame_id);
    base::lab::missing_algorithm("S01.Q03:R06");
    // LAB-S01.Q03:R06-END
}

auto LruReplacer::pool_size() const noexcept -> std::size_t {
    return implementation_->pool_size;
}

auto LruReplacer::bucket_count() const noexcept -> std::size_t {
    return implementation_->buckets.size();
}

struct ClockReplacer::Impl final {
    explicit Impl(const std::size_t pool_size_value)
        : pool_size{pool_size_value},
          usage(pool_size) {}

    [[maybe_unused]] auto
    decrement_if_positive([[maybe_unused]] const std::size_t index) const noexcept -> bool {
        // LAB-S01.Q02:R01-BEGIN
        static_cast<void>(index);
        base::lab::missing_algorithm("S01.Q02:R01");
        // LAB-S01.Q02:R01-END
    }

    std::size_t pool_size;
    mutable std::vector<std::atomic<std::uint32_t>> usage;
    std::atomic<std::size_t> clock_hand{0};
};

auto ClockReplacer::create(const std::size_t pool_size)
    -> base::Result<std::unique_ptr<ClockReplacer>> {
    if (pool_size == 0) {
        return base::fail(invalid("CLOCK replacement requires a non-empty pool"));
    }
    if (pool_size > std::numeric_limits<std::uint32_t>::max()) {
        return base::fail(invalid("replacement pool exceeds the FrameId domain"));
    }
    return std::unique_ptr<ClockReplacer>{new ClockReplacer{std::make_unique<Impl>(pool_size)}};
}

ClockReplacer::ClockReplacer(std::unique_ptr<Impl> implementation) noexcept
    : implementation_{std::move(implementation)} {}

ClockReplacer::~ClockReplacer() = default;

[[maybe_unused]] auto ClockReplacer::evict([[maybe_unused]] const std::span<Frame* const> frames)
    -> base::Result<PinGuard> {
    // LAB-S01.Q02:R02-BEGIN
    static_cast<void>(frames);
    return base::fail(missing_algorithm("S01.Q02:R02"));
    // LAB-S01.Q02:R02-END
}

[[maybe_unused]] void
ClockReplacer::on_pin([[maybe_unused]] const base::FrameId frame_id) noexcept {
    // LAB-S01.Q02:R03-BEGIN
    static_cast<void>(frame_id);
    base::lab::missing_algorithm("S01.Q02:R03");
    // LAB-S01.Q02:R03-END
}

[[maybe_unused]] void
ClockReplacer::on_remove([[maybe_unused]] const base::FrameId frame_id) noexcept {
    // LAB-S01.Q02:R04-BEGIN
    static_cast<void>(frame_id);
    base::lab::missing_algorithm("S01.Q02:R04");
    // LAB-S01.Q02:R04-END
}

auto ClockReplacer::usage(const base::FrameId frame_id) const -> base::Result<std::uint32_t> {
    auto index = frame_index(frame_id, implementation_->pool_size);
    if (!index) {
        return base::fail(std::move(index).error());
    }
    return implementation_->usage[index.value()].load(std::memory_order_acquire);
}

auto ClockReplacer::pool_size() const noexcept -> std::size_t {
    return implementation_->pool_size;
}

}  // namespace db::storage
