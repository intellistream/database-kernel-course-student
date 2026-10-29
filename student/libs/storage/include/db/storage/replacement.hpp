#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/frame.hpp"

namespace db::storage {

class ReplacementPolicy {
public:
    ReplacementPolicy() = default;
    ReplacementPolicy(const ReplacementPolicy&) = delete;
    auto operator=(const ReplacementPolicy&) -> ReplacementPolicy& = delete;
    ReplacementPolicy(ReplacementPolicy&&) = delete;
    auto operator=(ReplacementPolicy&&) -> ReplacementPolicy& = delete;
    virtual ~ReplacementPolicy() = default;

    // The returned guard is the victim claim: replacement may proceed only
    // while this pin remains alive. The frame directory is owned by
    // BufferPool, remains stable for the policy lifetime, and is supplied in
    // FrameId order. Victim selection is the policy's fallible boundary;
    // bookkeeping notifications receive BufferPool-owned FrameIds and must
    // not fail or throw.
    virtual auto evict(std::span<Frame* const> frames) -> base::Result<PinGuard> = 0;
    virtual void on_pin(base::FrameId frame_id) noexcept = 0;

    // Restores a victim removed by evict() after writeback or replacement
    // failed. This is distinct from on_pin() so policy state describes the
    // claim lifecycle explicitly; policies with identical bookkeeping may
    // use the default implementation.
    virtual void on_restore(base::FrameId frame_id) noexcept;

    // Removes a frame that is being retired by a relation drop. Custom
    // policies may override this to discard frame-local bookkeeping; the
    // default is valid for policies whose state is derived only on eviction.
    virtual void on_remove(base::FrameId frame_id) noexcept;
};

class LruReplacer final : public ReplacementPolicy {
public:
    static auto create(std::size_t pool_size, std::size_t bucket_count = 1)
        -> base::Result<std::unique_ptr<LruReplacer>>;

    LruReplacer(const LruReplacer&) = delete;
    auto operator=(const LruReplacer&) -> LruReplacer& = delete;
    LruReplacer(LruReplacer&&) = delete;
    auto operator=(LruReplacer&&) -> LruReplacer& = delete;
    ~LruReplacer() override;

    auto evict(std::span<Frame* const> frames) -> base::Result<PinGuard> override;
    void on_pin(base::FrameId frame_id) noexcept override;
    void on_remove(base::FrameId frame_id) noexcept override;
    auto pool_size() const noexcept -> std::size_t;
    auto bucket_count() const noexcept -> std::size_t;

private:
    struct Impl;

    explicit LruReplacer(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

class ClockReplacer final : public ReplacementPolicy {
public:
    static constexpr std::uint32_t kMaximumUsage = 5;

    static auto create(std::size_t pool_size) -> base::Result<std::unique_ptr<ClockReplacer>>;

    ClockReplacer(const ClockReplacer&) = delete;
    auto operator=(const ClockReplacer&) -> ClockReplacer& = delete;
    ClockReplacer(ClockReplacer&&) = delete;
    auto operator=(ClockReplacer&&) -> ClockReplacer& = delete;
    ~ClockReplacer() override;

    auto evict(std::span<Frame* const> frames) -> base::Result<PinGuard> override;
    void on_pin(base::FrameId frame_id) noexcept override;
    void on_remove(base::FrameId frame_id) noexcept override;
    auto usage(base::FrameId frame_id) const -> base::Result<std::uint32_t>;
    auto pool_size() const noexcept -> std::size_t;

private:
    struct Impl;

    explicit ClockReplacer(std::unique_ptr<Impl> implementation) noexcept;

    std::unique_ptr<Impl> implementation_;
};

}  // namespace db::storage
