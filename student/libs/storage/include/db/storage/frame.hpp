#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>

#include "db/base/identifier.hpp"
#include "db/base/result.hpp"
#include "db/storage/page.hpp"

namespace db::storage {

class Frame;
class PageReadGuard;
class PageWriteGuard;

// Page-content reader/writer latch. Writers are serialized by writer_mutex_;
// the atomic state supplies the uncontended reader fast path and blocks new
// readers once a writer is pending.
class FrameContentLatch final {
public:
    FrameContentLatch() = default;
    FrameContentLatch(const FrameContentLatch&) = delete;
    auto operator=(const FrameContentLatch&) -> FrameContentLatch& = delete;
    FrameContentLatch(FrameContentLatch&&) = delete;
    auto operator=(FrameContentLatch&&) -> FrameContentLatch& = delete;
    ~FrameContentLatch() = default;

    void lock_shared() noexcept;
    auto try_lock_shared() noexcept -> bool;
    void unlock_shared() noexcept;

    void lock();
    auto try_lock() -> bool;
    void unlock() noexcept;

private:
    static constexpr std::uint32_t kWriterLocked = 1U << 31U;
    static constexpr std::uint32_t kWriterPending = 1U << 30U;
    static constexpr std::uint32_t kReaderMask = kWriterPending - 1U;

    std::atomic<std::uint32_t> state_{0};
    std::mutex writer_mutex_;
};

struct FrameSnapshot final {
    std::optional<PageAddress> address;
    std::uint32_t pin_count{0};
    bool valid{false};
    bool dirty{false};
    bool just_dirtied{false};
    bool io_in_progress{false};
    bool replacement_claimed{false};
    bool checkpoint_needed{false};
};

class PinGuard final {
public:
    PinGuard(const PinGuard&) = delete;
    auto operator=(const PinGuard&) -> PinGuard& = delete;
    PinGuard(PinGuard&& other) noexcept;
    auto operator=(PinGuard&& other) noexcept -> PinGuard&;
    ~PinGuard();

    auto valid() const noexcept -> bool;
    auto frame_id() const noexcept -> base::FrameId;
    auto address() const -> std::optional<PageAddress>;
    auto read_latch() const& -> base::Result<PageReadGuard>;
    auto read_latch() && -> base::Result<PageReadGuard>;
    auto write_latch() const& -> base::Result<PageWriteGuard>;
    auto write_latch() && -> base::Result<PageWriteGuard>;
    auto try_write_latch() const& -> base::Result<PageWriteGuard>;
    auto try_write_latch() && -> base::Result<PageWriteGuard>;
    auto try_cleanup_latch() const& -> base::Result<PageWriteGuard>;
    auto try_cleanup_latch() && -> base::Result<PageWriteGuard>;

private:
    friend class Frame;

    explicit PinGuard(Frame& frame) noexcept;

    Frame* frame_;
};

class PageReadGuard final {
public:
    PageReadGuard(const PageReadGuard&) = delete;
    auto operator=(const PageReadGuard&) -> PageReadGuard& = delete;
    PageReadGuard(PageReadGuard&& other) noexcept;
    auto operator=(PageReadGuard&& other) noexcept -> PageReadGuard&;
    ~PageReadGuard() = default;

    auto valid() const noexcept -> bool;
    // Non-owning observer valid only while this guard owns its content latch.
    auto page() const noexcept -> const Page*;
    // The returned view is valid only while this guard owns its content latch.
    auto view() const noexcept -> std::optional<PageView>;

private:
    friend class Frame;

    PageReadGuard(
        PinGuard pin,
        std::shared_lock<FrameContentLatch> content_lock,
        const Page& page
    ) noexcept;

    // Declared before the latch so destruction releases the latch first.
    PinGuard pin_;
    std::shared_lock<FrameContentLatch> content_lock_;
    const Page* page_;
};

class PageWriteGuard final {
public:
    PageWriteGuard(const PageWriteGuard&) = delete;
    auto operator=(const PageWriteGuard&) -> PageWriteGuard& = delete;
    PageWriteGuard(PageWriteGuard&& other) noexcept;
    auto operator=(PageWriteGuard&&) noexcept -> PageWriteGuard& = delete;
    ~PageWriteGuard() = default;

    auto valid() const noexcept -> bool;
    // Non-owning observer valid only while this guard owns its content latch.
    auto page() const noexcept -> const Page*;
    auto page() noexcept -> Page*;
    // The returned view is valid only while this guard owns its content latch.
    auto view() const noexcept -> std::optional<PageView>;
    // The returned view is valid only while this guard owns its content latch.
    auto mutable_view() noexcept -> std::optional<MutablePageView>;

private:
    friend class Frame;

    PageWriteGuard(
        PinGuard pin,
        std::unique_lock<FrameContentLatch> content_lock,
        Page& page
    ) noexcept;

    // Declared before the latch so destruction releases the latch first.
    PinGuard pin_;
    std::unique_lock<FrameContentLatch> content_lock_;
    Page* page_;
};

class Frame final {
public:
    explicit Frame(base::FrameId frame_id) noexcept;
    Frame(const Frame&) = delete;
    auto operator=(const Frame&) -> Frame& = delete;
    Frame(Frame&&) = delete;
    auto operator=(Frame&&) -> Frame& = delete;
    ~Frame() = default;

    auto frame_id() const noexcept -> base::FrameId;
    auto snapshot() const -> FrameSnapshot;
    auto is_free() const -> bool;
    auto is_evictable() const -> bool;

    // Installs a page admitted by DiskManager into a free frame and returns
    // its first pin. Future cache hits use try_pin().
    auto install(PageAddress address, const Page& page, bool initially_dirty = false)
        -> base::Result<PinGuard>;
    // Returns an armed pin only while the published address and frame state
    // still match. A miss is expected during concurrent recycling, so it is
    // represented directly instead of constructing an error object.
    auto try_pin(PageAddress expected_address) -> std::optional<PinGuard>;
    auto try_claim_evictable() -> base::Result<PinGuard>;

    // Replaces an evictable frame while the caller owns its sole pin.
    auto replace(
        const PinGuard& claim,
        PageAddress address,
        const Page& page,
        bool initially_dirty = false
    ) -> base::Status;

    // Invalidates a sole replacement claim without writing the page. The
    // caller must hold the claim returned by try_claim_evictable(); the claim
    // remains armed until destruction so the frame can return to the free list
    // through the ordinary pin-release path.
    auto invalidate(const PinGuard& claim) -> base::Status;

    auto begin_io(const PinGuard& pin) -> base::Status;
    auto finish_io(const PinGuard& pin, bool succeeded) -> base::Status;
    void wait_while_io() const;

    auto mark_checkpoint_needed(const PinGuard& pin) -> base::Status;
    auto clear_checkpoint_needed(const PinGuard& pin) -> base::Status;

private:
    friend class PinGuard;

    auto owns(const PinGuard& pin) const noexcept -> bool;
    auto pinned_address() const noexcept -> std::optional<PageAddress>;
    void retain_pin() noexcept;
    void release_pin() noexcept;

    auto read_latch(const PinGuard& pin) -> base::Result<PageReadGuard>;
    auto adopt_read_latch(PinGuard pin) -> base::Result<PageReadGuard>;
    auto write_latch(const PinGuard& pin, bool try_only, bool cleanup)
        -> base::Result<PageWriteGuard>;
    auto adopt_write_latch(PinGuard pin, bool try_only, bool cleanup)
        -> base::Result<PageWriteGuard>;

    static constexpr std::uint64_t kPinCountMask = 0xFFFF'FFFFULL;
    static constexpr std::uint64_t kValid = 1ULL << 32U;
    static constexpr std::uint64_t kDirty = 1ULL << 33U;
    static constexpr std::uint64_t kJustDirtied = 1ULL << 34U;
    static constexpr std::uint64_t kIoInProgress = 1ULL << 35U;
    static constexpr std::uint64_t kReplacementClaimed = 1ULL << 36U;
    static constexpr std::uint64_t kCheckpointNeeded = 1ULL << 37U;
    static constexpr std::uint64_t kMetadataChanging = 1ULL << 38U;
    static constexpr std::uint64_t kGenerationOne = 1ULL << 39U;
    static constexpr std::uint64_t kGenerationMask = ~((1ULL << 39U) - 1U);
    static constexpr std::uint64_t kNoAddress = ~std::uint64_t{0};

    static auto encode_address(PageAddress address) noexcept -> std::uint64_t;
    static auto decode_address(std::uint64_t encoded) noexcept -> std::optional<PageAddress>;
    static auto next_generation(std::uint64_t state) noexcept -> std::uint64_t;

    base::FrameId frame_id_;
    alignas(64) std::atomic<std::uint64_t> state_{0};
    std::atomic<std::uint64_t> address_{kNoAddress};
    mutable FrameContentLatch content_mutex_;
    std::optional<Page> page_;
};

}  // namespace db::storage
