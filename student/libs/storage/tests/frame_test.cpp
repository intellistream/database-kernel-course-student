#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/storage/frame.hpp"
#include "db/storage/page.hpp"

namespace db::storage {
namespace {

constexpr PageAddress kAddress{base::RelationId{41}, base::PageId{3}};
constexpr PageAddress kOtherAddress{base::RelationId{41}, base::PageId{4}};

auto make_page(const PageAddress address) -> Page {
    auto page = Page::create(PageKind::kHeap, address);
    EXPECT_TRUE(page);
    return std::move(page).value();
}

TEST(FrameContentLatchTest, PendingWriterPreventsReaderBarging) {
    FrameContentLatch latch;
    latch.lock_shared();

    std::atomic<bool> writer_started{false};
    std::atomic<bool> writer_acquired{false};
    std::atomic<bool> release_writer{false};
    std::atomic<bool> late_reader_acquired{false};
    std::thread writer{[&] {
        writer_started.store(true, std::memory_order_release);
        latch.lock();
        writer_acquired.store(true, std::memory_order_release);
        while (!release_writer.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        latch.unlock();
    }};

    while (!writer_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    bool observed_pending = false;
    for (std::size_t attempt = 0; attempt < 100'000U; ++attempt) {
        if (!latch.try_lock_shared()) {
            observed_pending = true;
            break;
        }
        latch.unlock_shared();
        std::this_thread::yield();
    }

    std::thread late_reader{[&] {
        latch.lock_shared();
        late_reader_acquired.store(true, std::memory_order_release);
        latch.unlock_shared();
    }};
    for (std::size_t attempt = 0; attempt < 10'000U; ++attempt) {
        if (late_reader_acquired.load(std::memory_order_acquire)) {
            break;
        }
        std::this_thread::yield();
    }
    const bool reader_barged = late_reader_acquired.load(std::memory_order_acquire);

    latch.unlock_shared();
    while (!writer_acquired.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    const bool writer_preceded_reader = !late_reader_acquired.load(std::memory_order_acquire);
    release_writer.store(true, std::memory_order_release);
    writer.join();
    late_reader.join();

    EXPECT_TRUE(observed_pending);
    EXPECT_FALSE(reader_barged);
    EXPECT_TRUE(writer_preceded_reader);
    EXPECT_TRUE(late_reader_acquired.load(std::memory_order_acquire));
}

void expect_inert(const PinGuard& guard) {
    // Deliberately inspects the documented inert state after a move.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(guard.valid());
    EXPECT_EQ(guard.frame_id(), base::kInvalidFrameId);
    EXPECT_FALSE(guard.address().has_value());
    auto read = guard.read_latch();
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().code(), base::ErrorCode::kUnavailable);
}

void expect_inert(const PageReadGuard& guard) {
    // Deliberately inspects the documented inert state after a move.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(guard.valid());
    EXPECT_EQ(guard.page(), nullptr);
    EXPECT_FALSE(guard.view().has_value());
}

void expect_inert(PageWriteGuard& guard) {
    // Deliberately inspects the documented inert state after a move.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(guard.valid());
    EXPECT_EQ(guard.page(), nullptr);
    EXPECT_FALSE(guard.view().has_value());
    EXPECT_FALSE(guard.mutable_view().has_value());
}

TEST(FrameTest, InstallsPinsAndReleasesWithRaii) {
    Frame frame{base::FrameId{2}};
    EXPECT_TRUE(frame.is_free());
    EXPECT_FALSE(frame.is_evictable());
    {
        auto first = frame.install(kAddress, make_page(kAddress));
        ASSERT_TRUE(first);
        EXPECT_EQ(first.value().frame_id(), base::FrameId{2});
        EXPECT_EQ(first.value().address(), kAddress);
        EXPECT_EQ(frame.snapshot().pin_count, 1U);
        auto second = frame.try_pin(kAddress);
        ASSERT_TRUE(second);
        EXPECT_EQ(frame.snapshot().pin_count, 2U);
        EXPECT_FALSE(frame.try_pin(kOtherAddress));
    }
    EXPECT_EQ(frame.snapshot().pin_count, 0U);
    EXPECT_TRUE(frame.is_evictable());
}

TEST(FrameTest, LatchKeepsLogicalPinAliveAfterPinGuardMovesAway) {
    Frame frame{base::FrameId{0}};
    auto pin = frame.install(kAddress, make_page(kAddress));
    ASSERT_TRUE(pin);
    std::optional<PinGuard> pin_guard{std::move(pin).value()};
    std::optional<PageReadGuard> read;
    read.emplace(std::move(pin_guard->read_latch()).value());
    EXPECT_EQ(frame.snapshot().pin_count, 2U);
    pin_guard.reset();
    EXPECT_EQ(frame.snapshot().pin_count, 1U);
    ASSERT_TRUE(read->view().has_value());
    read.reset();
    EXPECT_EQ(frame.snapshot().pin_count, 0U);
}

TEST(FrameTest, PageViewsFollowLatchGuardOwnership) {
    Frame frame{base::FrameId{0}};
    auto installed = frame.install(kAddress, make_page(kAddress));
    ASSERT_TRUE(installed);

    auto latched = std::move(installed).value().write_latch();
    ASSERT_TRUE(latched);
    PageWriteGuard write{std::move(latched).value()};
    ASSERT_TRUE(write.view().has_value());
    ASSERT_TRUE(write.mutable_view().has_value());
    EXPECT_EQ(write.view()->bytes().data(), write.page()->bytes().data());

    {
        auto page = write.mutable_view();
        ASSERT_TRUE(page);
        page->set_flags(0xB2A1U);
        page->set_lsn(base::Lsn{77U});
    }

    PageWriteGuard moved_write{std::move(write)};
    // Deliberately verifies that a moved-from guard cannot issue a new view.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert(write);
    ASSERT_TRUE(moved_write.view().has_value());
    EXPECT_EQ(moved_write.view()->flags(), 0xB2A1U);
    EXPECT_EQ(moved_write.view()->lsn(), base::Lsn{77U});
}

TEST(FrameTest, RvalueLatchAdoptsPinWithoutAddingASecondPhysicalPin) {
    Frame frame{base::FrameId{0}};
    auto pin = frame.install(kAddress, make_page(kAddress));
    ASSERT_TRUE(pin);

    auto latched = std::move(pin).value().read_latch();
    ASSERT_TRUE(latched);
    std::optional<PageReadGuard> read{std::move(latched).value()};
    EXPECT_EQ(frame.snapshot().pin_count, 1U);
    // Deliberately verifies ownership transfer out of the Result-held guard.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(pin.value().valid());

    read.reset();
    EXPECT_EQ(frame.snapshot().pin_count, 0U);
}

TEST(FrameTest, PinMoveAssignmentReleasesThePreviousFrame) {
    Frame first_frame{base::FrameId{0}};
    Frame second_frame{base::FrameId{1}};
    auto first = first_frame.install(kAddress, make_page(kAddress));
    auto second = second_frame.install(kOtherAddress, make_page(kOtherAddress));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    first.value() = std::move(second).value();
    EXPECT_EQ(first_frame.snapshot().pin_count, 0U);
    EXPECT_EQ(second_frame.snapshot().pin_count, 1U);
    EXPECT_EQ(first.value().frame_id(), base::FrameId{1});
    // Deliberately verifies the moved-from Result-held guard.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(second.value().valid());
}

TEST(FrameTest, ReadMoveAssignmentReleasesThePreviousLatch) {
    Frame first_frame{base::FrameId{0}};
    Frame second_frame{base::FrameId{1}};
    auto first = first_frame.install(kAddress, make_page(kAddress));
    auto second = second_frame.install(kOtherAddress, make_page(kOtherAddress));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    auto first_read = std::move(first).value().read_latch();
    auto second_read = std::move(second).value().read_latch();
    ASSERT_TRUE(first_read);
    ASSERT_TRUE(second_read);
    PageReadGuard target{std::move(first_read).value()};
    PageReadGuard replacement{std::move(second_read).value()};

    target = std::move(replacement);
    EXPECT_EQ(first_frame.snapshot().pin_count, 0U);
    EXPECT_EQ(second_frame.snapshot().pin_count, 1U);
    EXPECT_TRUE(target.valid());
    // Deliberately verifies the moved-from guard after latch transfer.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_FALSE(replacement.valid());
}

TEST(FrameTest, ReadLatchesShareAndWriteLatchMarksDirtyBeforeMutation) {
    Frame frame{base::FrameId{0}};
    auto pin = frame.install(kAddress, make_page(kAddress));
    ASSERT_TRUE(pin);
    auto competing_pin = frame.try_pin(kAddress);
    ASSERT_TRUE(competing_pin);
    std::optional<PageReadGuard> first_read{
        std::move(pin.value().read_latch()).value(),
    };
    std::optional<PageReadGuard> second_read{
        std::move(competing_pin.value().read_latch()).value(),
    };
    EXPECT_FALSE(frame.snapshot().dirty);
    const auto blocked = pin.value().try_write_latch();
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code(), base::ErrorCode::kUnavailable);
    first_read.reset();
    second_read.reset();

    auto write = pin.value().write_latch();
    ASSERT_TRUE(write);
    EXPECT_TRUE(frame.snapshot().dirty);
    EXPECT_TRUE(frame.snapshot().just_dirtied);
    auto byte = write.value().page()->mutable_view(100, 1);
    ASSERT_TRUE(byte);
    byte.value().front() = std::byte{0xAC};
}

TEST(FrameTest, MovedFromGuardsCannotExposeAnUnlatchedPage) {
    Frame frame{base::FrameId{0}};
    auto installed = frame.install(kAddress, make_page(kAddress));
    ASSERT_TRUE(installed);
    PinGuard pin{std::move(installed).value()};
    PinGuard moved_pin{std::move(pin)};
    // Deliberately verifies each guard's documented moved-from contract.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert(pin);
    ASSERT_TRUE(moved_pin.valid());
    EXPECT_EQ(moved_pin.address(), kAddress);

    {
        auto latched = moved_pin.read_latch();
        ASSERT_TRUE(latched);
        PageReadGuard read{std::move(latched).value()};
        const Page* const page = read.page();
        ASSERT_NE(page, nullptr);
        PageReadGuard moved_read{std::move(read)};
        // Deliberately verifies the moved-from observer after latch transfer.
        // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
        expect_inert(read);
        EXPECT_TRUE(moved_read.valid());
        EXPECT_EQ(moved_read.page(), page);
    }

    auto latched = moved_pin.write_latch();
    ASSERT_TRUE(latched);
    PageWriteGuard write{std::move(latched).value()};
    Page* const page = write.page();
    ASSERT_NE(page, nullptr);
    PageWriteGuard moved_write{std::move(write)};
    // Deliberately verifies the moved-from observer after latch transfer.
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    expect_inert(write);
    EXPECT_TRUE(moved_write.valid());
    EXPECT_EQ(moved_write.page(), page);
}

TEST(FrameTest, CleanupLatchRequiresExclusiveContentAndSolePin) {
    Frame frame{base::FrameId{0}};
    auto cleanup_pin = frame.install(kAddress, make_page(kAddress));
    ASSERT_TRUE(cleanup_pin);
    auto competing_pin = frame.try_pin(kAddress);
    ASSERT_TRUE(competing_pin);
    const auto competing = cleanup_pin.value().try_cleanup_latch();
    ASSERT_FALSE(competing);
    EXPECT_EQ(competing.error().code(), base::ErrorCode::kUnavailable);
    std::optional<PinGuard> competing_guard{
        std::in_place,
        std::move(competing_pin).value(),
    };
    competing_guard.reset();

    auto reader_pin = frame.try_pin(kAddress);
    ASSERT_TRUE(reader_pin);
    std::optional<PinGuard> reader_guard{
        std::in_place,
        std::move(reader_pin).value(),
    };
    std::optional<PageReadGuard> read{
        std::move(reader_guard->read_latch()).value(),
    };
    reader_guard.reset();
    const auto busy = cleanup_pin.value().try_cleanup_latch();
    ASSERT_FALSE(busy);
    EXPECT_EQ(busy.error().code(), base::ErrorCode::kUnavailable);
    read.reset();

    auto cleanup = cleanup_pin.value().try_cleanup_latch();
    EXPECT_TRUE(cleanup);
}

TEST(FrameTest, WritebackPreservesRedirtyAndWakesWaiters) {
    Frame frame{base::FrameId{0}};
    auto io_pin = frame.install(kAddress, make_page(kAddress), true);
    ASSERT_TRUE(io_pin);
    ASSERT_TRUE(frame.mark_checkpoint_needed(io_pin.value()));
    ASSERT_TRUE(frame.begin_io(io_pin.value()));
    EXPECT_TRUE(frame.snapshot().io_in_progress);
    EXPECT_FALSE(frame.try_pin(kAddress));

    std::atomic waiting{false};
    std::atomic awakened{false};
    std::thread waiter{[&] {
        waiting.store(true);
        frame.wait_while_io();
        awakened.store(true);
    }};
    while (!waiting.load()) {
        std::this_thread::yield();
    }
    {
        auto write = io_pin.value().write_latch();
        ASSERT_TRUE(write);
    }
    EXPECT_TRUE(frame.snapshot().just_dirtied);
    EXPECT_TRUE(frame.finish_io(io_pin.value(), true));
    waiter.join();
    EXPECT_TRUE(awakened.load());
    EXPECT_TRUE(frame.snapshot().dirty);
    EXPECT_TRUE(frame.snapshot().checkpoint_needed);

    ASSERT_TRUE(frame.begin_io(io_pin.value()));
    EXPECT_TRUE(frame.finish_io(io_pin.value(), true));
    EXPECT_FALSE(frame.snapshot().dirty);
    EXPECT_FALSE(frame.snapshot().checkpoint_needed);
}

TEST(FrameTest, EvictionClaimAndReplacementRequireSolePin) {
    Frame frame{base::FrameId{7}};
    {
        auto pin = frame.install(kAddress, make_page(kAddress));
        ASSERT_TRUE(pin);
        EXPECT_FALSE(frame.try_claim_evictable());
    }
    auto claim = frame.try_claim_evictable();
    ASSERT_TRUE(claim);
    EXPECT_EQ(frame.snapshot().pin_count, 1U);
    auto extra = frame.try_pin(kAddress);
    EXPECT_FALSE(extra);
    EXPECT_TRUE(frame.replace(claim.value(), kOtherAddress, make_page(kOtherAddress), true));
    EXPECT_EQ(claim.value().address(), kOtherAddress);
    EXPECT_TRUE(frame.snapshot().dirty);
}

TEST(FrameTest, ConcurrentPinsReturnToAnEvictableState) {
    Frame frame{base::FrameId{8}};
    {
        auto installed = frame.install(kAddress, make_page(kAddress));
        ASSERT_TRUE(installed);
    }

    constexpr std::size_t thread_count = 8U;
    constexpr std::size_t pins_per_thread = 10'000U;
    std::atomic<bool> start{false};
    std::atomic<std::size_t> failures{0U};
    std::array<std::thread, thread_count> workers;
    for (auto& worker : workers) {
        worker = std::thread{[&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (std::size_t pin_index = 0; pin_index < pins_per_thread; ++pin_index) {
                auto pin = frame.try_pin(kAddress);
                if (!pin) {
                    failures.fetch_add(1U, std::memory_order_relaxed);
                }
            }
        }};
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) {
        worker.join();
    }

    EXPECT_EQ(failures.load(std::memory_order_relaxed), 0U);
    EXPECT_EQ(frame.snapshot().pin_count, 0U);
    EXPECT_TRUE(frame.is_evictable());
}

TEST(FrameTest, SnapshotsRemainCoherentAcrossAddressGenerations) {
    Frame frame{base::FrameId{9}};
    {
        auto installed = frame.install(kAddress, make_page(kAddress));
        ASSERT_TRUE(installed);
    }

    std::atomic<bool> stopped{false};
    std::atomic<bool> coherent{true};
    std::thread observer{[&] {
        while (!stopped.load(std::memory_order_acquire)) {
            const auto state = frame.snapshot();
            const bool matches_generation = state.valid && state.address.has_value()
                                            && ((state.address == kAddress && !state.dirty)
                                                || (state.address == kOtherAddress && state.dirty));
            if (!matches_generation) {
                coherent.store(false, std::memory_order_relaxed);
            }
        }
    }};

    constexpr std::size_t replacement_count = 10'000U;
    for (std::size_t replacement = 0; replacement < replacement_count; ++replacement) {
        auto claim = frame.try_claim_evictable();
        if (!claim) {
            coherent.store(false, std::memory_order_relaxed);
            break;
        }
        const bool use_other = (replacement & 1U) == 0U;
        const auto address = use_other ? kOtherAddress : kAddress;
        if (!frame.replace(claim.value(), address, make_page(address), use_other)) {
            coherent.store(false, std::memory_order_relaxed);
            break;
        }
    }
    stopped.store(true, std::memory_order_release);
    observer.join();

    EXPECT_TRUE(coherent.load(std::memory_order_relaxed));
}

TEST(FrameTest, RejectsForeignClaims) {
    Frame first{base::FrameId{1}};
    Frame second{base::FrameId{2}};
    auto pin = first.install(kAddress, make_page(kAddress));
    ASSERT_TRUE(pin);
    EXPECT_FALSE(second.begin_io(pin.value()));
    EXPECT_FALSE(second.mark_checkpoint_needed(pin.value()));
}

}  // namespace
}  // namespace db::storage
