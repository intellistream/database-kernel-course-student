#include "db/storage/frame.hpp"

#include <utility>

#include "db/base/error.hpp"
#include "db/base/lab_missing.hpp"

namespace db::storage {
namespace {

[[maybe_unused]] auto unavailable(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kUnavailable, std::move(message)};
}

[[maybe_unused]] auto invariant(std::string message) -> base::Error {
    return base::Error{base::ErrorCode::kInvariantViolation, std::move(message)};
}

}  // namespace

void FrameContentLatch::lock_shared() noexcept {
    auto current = state_.load(std::memory_order_acquire);
    for (;;) {
        if ((current & (kWriterLocked | kWriterPending)) == 0) {
            if ((current & kReaderMask) == kReaderMask) {
                std::terminate();
            }
            if (state_.compare_exchange_weak(
                    current,
                    current + 1U,
                    std::memory_order_acquire,
                    std::memory_order_relaxed
                )) {
                return;
            }
            continue;
        }
        state_.wait(current, std::memory_order_relaxed);
        current = state_.load(std::memory_order_acquire);
    }
}

auto FrameContentLatch::try_lock_shared() noexcept -> bool {
    auto current = state_.load(std::memory_order_acquire);
    while ((current & (kWriterLocked | kWriterPending)) == 0) {
        if ((current & kReaderMask) == kReaderMask) {
            std::terminate();
        }
        if (state_.compare_exchange_weak(
                current,
                current + 1U,
                std::memory_order_acquire,
                std::memory_order_relaxed
            )) {
            return true;
        }
    }
    return false;
}

void FrameContentLatch::unlock_shared() noexcept {
    const auto previous = state_.fetch_sub(1U, std::memory_order_release);
    if ((previous & kReaderMask) == 0 || (previous & kWriterLocked) != 0) {
        std::terminate();
    }
    if ((previous & kReaderMask) == 1U && (previous & kWriterPending) != 0) {
        state_.notify_all();
    }
}

void FrameContentLatch::lock() {
    writer_mutex_.lock();
    auto current = state_.fetch_or(kWriterPending, std::memory_order_acq_rel) | kWriterPending;
    while ((current & kReaderMask) != 0) {
        state_.wait(current, std::memory_order_relaxed);
        current = state_.load(std::memory_order_acquire);
    }
    if (current != kWriterPending) {
        std::terminate();
    }
    state_.store(kWriterLocked, std::memory_order_relaxed);
}

auto FrameContentLatch::try_lock() -> bool {
    if (!writer_mutex_.try_lock()) {
        return false;
    }
    auto expected = std::uint32_t{0};
    if (state_.compare_exchange_strong(
            expected,
            kWriterLocked,
            std::memory_order_acquire,
            std::memory_order_relaxed
        )) {
        return true;
    }
    writer_mutex_.unlock();
    return false;
}

void FrameContentLatch::unlock() noexcept {
    if (state_.load(std::memory_order_relaxed) != kWriterLocked) {
        std::terminate();
    }
    state_.store(0, std::memory_order_release);
    state_.notify_all();
    writer_mutex_.unlock();
}

PinGuard::PinGuard(Frame& frame) noexcept
    : frame_{&frame} {}

PinGuard::PinGuard(PinGuard&& other) noexcept
    : frame_{std::exchange(other.frame_, nullptr)} {}

auto PinGuard::operator=(PinGuard&& other) noexcept -> PinGuard& {
    if (this != &other) {
        if (frame_ != nullptr) {
            frame_->release_pin();
        }
        frame_ = std::exchange(other.frame_, nullptr);
    }
    return *this;
}

PinGuard::~PinGuard() {
    if (frame_ != nullptr) {
        frame_->release_pin();
    }
}

auto PinGuard::valid() const noexcept -> bool {
    return frame_ != nullptr;
}

auto PinGuard::frame_id() const noexcept -> base::FrameId {
    return valid() ? frame_->frame_id() : base::kInvalidFrameId;
}

auto PinGuard::address() const -> std::optional<PageAddress> {
    if (!valid()) {
        return std::nullopt;
    }
    return frame_->pinned_address();
}

auto PinGuard::read_latch() const& -> base::Result<PageReadGuard> {
    if (!valid()) {
        return base::fail(unavailable("read latch requires an active pin guard"));
    }
    return frame_->read_latch(*this);
}

auto PinGuard::read_latch() && -> base::Result<PageReadGuard> {
    if (!valid()) {
        return base::fail(unavailable("read latch requires an active pin guard"));
    }
    return frame_->adopt_read_latch(std::move(*this));
}

auto PinGuard::write_latch() const& -> base::Result<PageWriteGuard> {
    if (!valid()) {
        return base::fail(unavailable("write latch requires an active pin guard"));
    }
    return frame_->write_latch(*this, false, false);
}

auto PinGuard::write_latch() && -> base::Result<PageWriteGuard> {
    if (!valid()) {
        return base::fail(unavailable("write latch requires an active pin guard"));
    }
    return frame_->adopt_write_latch(std::move(*this), false, false);
}

auto PinGuard::try_write_latch() const& -> base::Result<PageWriteGuard> {
    if (!valid()) {
        return base::fail(unavailable("write latch requires an active pin guard"));
    }
    return frame_->write_latch(*this, true, false);
}

auto PinGuard::try_write_latch() && -> base::Result<PageWriteGuard> {
    if (!valid()) {
        return base::fail(unavailable("write latch requires an active pin guard"));
    }
    return frame_->adopt_write_latch(std::move(*this), true, false);
}

auto PinGuard::try_cleanup_latch() const& -> base::Result<PageWriteGuard> {
    if (!valid()) {
        return base::fail(unavailable("cleanup latch requires an active pin guard"));
    }
    return frame_->write_latch(*this, true, true);
}

auto PinGuard::try_cleanup_latch() && -> base::Result<PageWriteGuard> {
    if (!valid()) {
        return base::fail(unavailable("cleanup latch requires an active pin guard"));
    }
    return frame_->adopt_write_latch(std::move(*this), true, true);
}

PageReadGuard::PageReadGuard(
    PinGuard pin,
    std::shared_lock<FrameContentLatch> content_lock,
    const Page& page
) noexcept
    : pin_{std::move(pin)},
      content_lock_{std::move(content_lock)},
      page_{&page} {}

PageReadGuard::PageReadGuard(PageReadGuard&& other) noexcept
    : pin_{std::move(other.pin_)},
      content_lock_{std::move(other.content_lock_)},
      page_{std::exchange(other.page_, nullptr)} {}

auto PageReadGuard::operator=(PageReadGuard&& other) noexcept -> PageReadGuard& {
    if (this != &other) {
        // Release the old content latch before releasing its pin, then take
        // ownership of the already-latched page. This preserves the same
        // lock-before-pin destruction order as the guard destructor.
        content_lock_ = std::move(other.content_lock_);
        pin_ = std::move(other.pin_);
        page_ = std::exchange(other.page_, nullptr);
    }
    return *this;
}

auto PageReadGuard::valid() const noexcept -> bool {
    return pin_.valid() && content_lock_.owns_lock() && page_ != nullptr;
}

auto PageReadGuard::page() const noexcept -> const Page* {
    return valid() ? page_ : nullptr;
}

auto PageReadGuard::view() const noexcept -> std::optional<PageView> {
    if (!valid()) {
        return std::nullopt;
    }
    return page_->view();
}

PageWriteGuard::PageWriteGuard(
    PinGuard pin,
    std::unique_lock<FrameContentLatch> content_lock,
    Page& page
) noexcept
    : pin_{std::move(pin)},
      content_lock_{std::move(content_lock)},
      page_{&page} {}

PageWriteGuard::PageWriteGuard(PageWriteGuard&& other) noexcept
    : pin_{std::move(other.pin_)},
      content_lock_{std::move(other.content_lock_)},
      page_{std::exchange(other.page_, nullptr)} {}

auto PageWriteGuard::valid() const noexcept -> bool {
    return pin_.valid() && content_lock_.owns_lock() && page_ != nullptr;
}

auto PageWriteGuard::page() const noexcept -> const Page* {
    return valid() ? page_ : nullptr;
}

auto PageWriteGuard::page() noexcept -> Page* {
    return valid() ? page_ : nullptr;
}

auto PageWriteGuard::view() const noexcept -> std::optional<PageView> {
    if (!valid()) {
        return std::nullopt;
    }
    return page_->view();
}

auto PageWriteGuard::mutable_view() noexcept -> std::optional<MutablePageView> {
    if (!valid()) {
        return std::nullopt;
    }
    return page_->mutable_view();
}

Frame::Frame(const base::FrameId frame_id) noexcept
    : frame_id_{frame_id} {}

auto Frame::frame_id() const noexcept -> base::FrameId {
    return frame_id_;
}

auto Frame::encode_address(const PageAddress address) noexcept -> std::uint64_t {
    return (static_cast<std::uint64_t>(address.relation_id.value()) << 32U)
           | address.page_id.value();
}

auto Frame::decode_address(const std::uint64_t encoded) noexcept -> std::optional<PageAddress> {
    if (encoded == kNoAddress) {
        return std::nullopt;
    }
    return PageAddress{
        base::RelationId{static_cast<std::uint32_t>(encoded >> 32U)},
        base::PageId{static_cast<std::uint32_t>(encoded)},
    };
}

[[maybe_unused]] auto Frame::next_generation([[maybe_unused]] const std::uint64_t state) noexcept
    -> std::uint64_t {
    // LAB-S01.Q05:R13-BEGIN
    base::lab::missing_algorithm("S01.Q05:R13");
    // LAB-S01.Q05:R13-END
}

[[maybe_unused]] auto Frame::snapshot() const -> FrameSnapshot {
    // LAB-S01.Q04:R27-BEGIN
    base::lab::missing_algorithm("S01.Q04:R27");
    // LAB-S01.Q04:R27-END
}

[[maybe_unused]] auto Frame::is_free() const -> bool {
    // LAB-S01.Q04:R28-BEGIN
    base::lab::missing_algorithm("S01.Q04:R28");
    // LAB-S01.Q04:R28-END
}

[[maybe_unused]] auto Frame::is_evictable() const -> bool {
    // LAB-S01.Q04:R29-BEGIN
    base::lab::missing_algorithm("S01.Q04:R29");
    // LAB-S01.Q04:R29-END
}

[[maybe_unused]] auto Frame::install(
    [[maybe_unused]] const PageAddress address,
    [[maybe_unused]] const Page& page,
    [[maybe_unused]] const bool initially_dirty
) -> base::Result<PinGuard> {
    // LAB-S01.Q04:R30-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R30"});
    // LAB-S01.Q04:R30-END
}

[[maybe_unused]] auto Frame::try_pin([[maybe_unused]] const PageAddress expected_address)
    -> std::optional<PinGuard> {
    // LAB-S01.Q04:R31-BEGIN
    base::lab::missing_algorithm("S01.Q04:R31");
    // LAB-S01.Q04:R31-END
}

[[maybe_unused]] auto Frame::try_claim_evictable() -> base::Result<PinGuard> {
    // LAB-S01.Q04:R32-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R32"});
    // LAB-S01.Q04:R32-END
}

[[maybe_unused]] auto Frame::replace(
    [[maybe_unused]] const PinGuard& claim,
    [[maybe_unused]] const PageAddress address,
    [[maybe_unused]] const Page& page,
    [[maybe_unused]] const bool initially_dirty
) -> base::Status {
    // LAB-S01.Q04:R33-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R33"});
    // LAB-S01.Q04:R33-END
}

[[maybe_unused]] auto Frame::invalidate([[maybe_unused]] const PinGuard& claim) -> base::Status {
    // LAB-S01.Q04:R34-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R34"});
    // LAB-S01.Q04:R34-END
}

[[maybe_unused]] auto Frame::begin_io([[maybe_unused]] const PinGuard& pin) -> base::Status {
    // LAB-S01.Q05:R14-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R14"});
    // LAB-S01.Q05:R14-END
}

[[maybe_unused]] auto
Frame::finish_io([[maybe_unused]] const PinGuard& pin, [[maybe_unused]] const bool succeeded)
    -> base::Status {
    // LAB-S01.Q05:R15-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R15"});
    // LAB-S01.Q05:R15-END
}

[[maybe_unused]] void Frame::wait_while_io() const {
    // LAB-S01.Q04:R35-BEGIN
    base::lab::missing_algorithm("S01.Q04:R35");
    // LAB-S01.Q04:R35-END
}

[[maybe_unused]] auto Frame::mark_checkpoint_needed([[maybe_unused]] const PinGuard& pin)
    -> base::Status {
    // LAB-S01.Q05:R16-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R16"});
    // LAB-S01.Q05:R16-END
}

[[maybe_unused]] auto Frame::clear_checkpoint_needed([[maybe_unused]] const PinGuard& pin)
    -> base::Status {
    // LAB-S01.Q05:R17-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q05:R17"});
    // LAB-S01.Q05:R17-END
}

[[maybe_unused]] auto Frame::owns([[maybe_unused]] const PinGuard& pin) const noexcept -> bool {
    // LAB-S01.Q04:R36-BEGIN
    base::lab::missing_algorithm("S01.Q04:R36");
    // LAB-S01.Q04:R36-END
}

[[maybe_unused]] auto Frame::pinned_address() const noexcept -> std::optional<PageAddress> {
    // LAB-S01.Q04:R37-BEGIN
    base::lab::missing_algorithm("S01.Q04:R37");
    // LAB-S01.Q04:R37-END
}

[[maybe_unused]] void Frame::retain_pin() noexcept {
    // LAB-S01.Q04:R38-BEGIN
    base::lab::missing_algorithm("S01.Q04:R38");
    // LAB-S01.Q04:R38-END
}

void Frame::release_pin() noexcept {
    const auto previous = state_.fetch_sub(1U, std::memory_order_acq_rel);
    const auto pin_count = previous & kPinCountMask;
    if (pin_count == 0) {
        std::terminate();
    }
    if (pin_count == 1U && (previous & kReplacementClaimed) != 0) {
        state_.fetch_and(~kReplacementClaimed, std::memory_order_release);
    }
}

[[maybe_unused]] auto Frame::read_latch([[maybe_unused]] const PinGuard& pin)
    -> base::Result<PageReadGuard> {
    // LAB-S01.Q04:R39-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R39"});
    // LAB-S01.Q04:R39-END
}

[[maybe_unused]] auto Frame::adopt_read_latch([[maybe_unused]] PinGuard pin)
    -> base::Result<PageReadGuard> {
    // LAB-S01.Q04:R40-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R40"});
    // LAB-S01.Q04:R40-END
}

[[maybe_unused]] auto Frame::write_latch(
    [[maybe_unused]] const PinGuard& pin,
    [[maybe_unused]] const bool try_only,
    [[maybe_unused]] const bool cleanup
) -> base::Result<PageWriteGuard> {
    // LAB-S01.Q04:R41-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R41"});
    // LAB-S01.Q04:R41-END
}

[[maybe_unused]] auto Frame::adopt_write_latch(
    [[maybe_unused]] PinGuard pin,
    [[maybe_unused]] const bool try_only,
    [[maybe_unused]] const bool cleanup
) -> base::Result<PageWriteGuard> {
    // LAB-S01.Q04:R42-BEGIN
    return base::fail(base::Error{base::ErrorCode::kUnsupported, "S01.Q04:R42"});
    // LAB-S01.Q04:R42-END
}

}  // namespace db::storage
