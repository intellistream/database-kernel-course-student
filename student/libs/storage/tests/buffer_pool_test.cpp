#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/base/error.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/page.hpp"

namespace db::storage {
namespace {

enum class DurabilityEventKind : std::uint8_t {
    kWal,
    kData,
};

struct DurabilityEvent final {
    DurabilityEventKind kind;
    base::Lsn lsn;

    auto operator==(const DurabilityEvent&) const noexcept -> bool = default;
};

class DurabilityTrace final {
public:
    void record(const DurabilityEvent event) {
        const std::lock_guard lock{mutex_};
        events_.push_back(event);
    }

    auto snapshot() const -> std::vector<DurabilityEvent> {
        const std::lock_guard lock{mutex_};
        return events_;
    }

private:
    mutable std::mutex mutex_;
    std::vector<DurabilityEvent> events_;
};

class RecordingDurability final : public PageDurabilityPort {
public:
    explicit RecordingDurability(
        DurabilityTrace& trace,
        const bool fail = false,
        const bool throw_next = false
    ) noexcept
        : trace_{trace},
          fail_{fail},
          throw_next_{throw_next} {}

    auto flush_through(const base::Lsn target_lsn) -> base::Status override {
        trace_.record(DurabilityEvent{DurabilityEventKind::kWal, target_lsn});
        if (throw_next_.exchange(false, std::memory_order_acq_rel)) {
            throw std::runtime_error{"injected WAL flush exception"};
        }
        if (fail_) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kIo,
                    "injected WAL flush failure",
                }
            );
        }
        return {};
    }

private:
    DurabilityTrace& trace_;
    bool fail_;
    std::atomic_bool throw_next_;
};

class MockDiskManager final : public DiskManager {
public:
    explicit MockDiskManager(DurabilityTrace* const trace = nullptr) noexcept
        : trace_{trace} {}

    auto seed(const base::RelationId relation_id, const PageKind kind = PageKind::kHeap)
        -> base::Result<PageAddress> {
        return allocate_page(relation_id, kind);
    }

    auto read_page(const PageAddress address) -> base::Result<Page> override {
        read_count.fetch_add(1);
        {
            std::unique_lock lock{read_mutex_};
            if (block_reads_) {
                ++blocked_read_count_;
                read_changed_.notify_all();
                read_changed_.wait(lock, [this] {
                    return !block_reads_;
                });
            }
        }
        if (throw_reads.exchange(false, std::memory_order_acq_rel)) {
            throw std::runtime_error{"injected mock read exception"};
        }
        if (fail_reads.load()) {
            return base::fail(base::Error{base::ErrorCode::kIo, "injected mock read failure"});
        }
        Page::Storage bytes;
        {
            const std::lock_guard lock{pages_mutex_};
            const auto found = pages_.find(key(address));
            if (found == pages_.end()) {
                return base::fail(base::Error{base::ErrorCode::kNotFound, "mock page not found"});
            }
            bytes = found->second;
        }
        return Page::load(bytes, address);
    }

    auto write_page(const PageAddress address, const Page& page) -> base::Status override {
        write_count.fetch_add(1);
        if (throw_writes.exchange(false, std::memory_order_acq_rel)) {
            throw std::runtime_error{"injected mock write exception"};
        }
        if (trace_ != nullptr) {
            trace_->record(DurabilityEvent{DurabilityEventKind::kData, page.lsn()});
        }
        {
            std::unique_lock lock{write_mutex_};
            write_started_ = true;
            write_changed_.notify_all();
            write_changed_.wait(lock, [this] {
                return !block_writes_;
            });
        }
        if (fail_writes.load()) {
            return base::fail(base::Error{base::ErrorCode::kIo, "injected mock write failure"});
        }
        const std::lock_guard lock{pages_mutex_};
        pages_[key(address)] = page.bytes();
        return {};
    }

    auto allocate_page(
        const base::RelationId relation_id,
        const PageKind kind,
        const std::size_t special_size = 0
    ) -> base::Result<PageAddress> override {
        if (throw_allocations.exchange(false, std::memory_order_acq_rel)) {
            throw std::bad_alloc{};
        }
        if (fail_allocations.load()) {
            return base::fail(
                base::Error{base::ErrorCode::kIo, "injected mock allocation failure"}
            );
        }
        const std::lock_guard lock{pages_mutex_};
        auto& next = next_page_[relation_id.value()];
        const PageAddress address{relation_id, base::PageId{next}};
        auto page = Page::create(kind, address, special_size);
        if (!page) {
            return base::fail(std::move(page).error());
        }
        pages_[key(address)] = page.value().bytes();
        ++next;
        return address;
    }

    auto page_count(const base::RelationId relation_id) -> base::Result<std::uint64_t> override {
        if (throw_page_counts.exchange(false, std::memory_order_acq_rel)) {
            throw std::runtime_error{"injected mock page-count exception"};
        }
        const std::lock_guard lock{pages_mutex_};
        const auto found = next_page_.find(relation_id.value());
        return found == next_page_.end() ? 0U : found->second;
    }

    auto sync_relation(const base::RelationId relation_id) -> base::Status override {
        static_cast<void>(relation_id);
        sync_count.fetch_add(1);
        if (throw_sync_relation.exchange(false, std::memory_order_acq_rel)) {
            throw std::runtime_error{"injected mock relation-sync exception"};
        }
        if (fail_sync.load()) {
            return base::fail(base::Error{base::ErrorCode::kIo, "injected mock sync failure"});
        }
        return {};
    }

    auto sync_all() -> base::Status override {
        sync_count.fetch_add(1);
        if (throw_sync_all.exchange(false, std::memory_order_acq_rel)) {
            throw std::runtime_error{"injected mock global-sync exception"};
        }
        if (fail_sync.load()) {
            return base::fail(base::Error{base::ErrorCode::kIo, "injected mock sync failure"});
        }
        return {};
    }

    auto remove_relation(const base::RelationId relation_id, const bool missing_ok)
        -> base::Status override {
        static_cast<void>(missing_ok);
        if (throw_removals.exchange(false, std::memory_order_acq_rel)) {
            throw std::runtime_error{"injected mock relation-removal exception"};
        }
        const std::lock_guard lock{pages_mutex_};
        for (auto iterator = pages_.begin(); iterator != pages_.end();) {
            if (iterator->first.first == relation_id.value()) {
                iterator = pages_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        next_page_.erase(relation_id.value());
        return {};
    }

    auto persisted_byte(const PageAddress address, const std::size_t offset) -> std::byte {
        const std::lock_guard lock{pages_mutex_};
        return pages_.at(key(address)).at(offset);
    }

    void block_reads() {
        const std::lock_guard lock{read_mutex_};
        block_reads_ = true;
        blocked_read_count_ = 0U;
    }

    void wait_for_blocked_reads(const std::size_t count) {
        std::unique_lock lock{read_mutex_};
        read_changed_.wait(lock, [this, count] {
            return blocked_read_count_ >= count;
        });
    }

    void release_reads() {
        {
            const std::lock_guard lock{read_mutex_};
            block_reads_ = false;
        }
        read_changed_.notify_all();
    }

    void block_writes() {
        const std::lock_guard lock{write_mutex_};
        block_writes_ = true;
        write_started_ = false;
    }

    void wait_for_write() {
        std::unique_lock lock{write_mutex_};
        write_changed_.wait(lock, [this] {
            return write_started_;
        });
    }

    void release_writes() {
        {
            const std::lock_guard lock{write_mutex_};
            block_writes_ = false;
        }
        write_changed_.notify_all();
    }

    std::atomic<std::size_t> read_count{0};
    std::atomic<std::size_t> write_count{0};
    std::atomic<std::size_t> sync_count{0};
    std::atomic<bool> fail_reads{false};
    std::atomic<bool> fail_writes{false};
    std::atomic<bool> fail_allocations{false};
    std::atomic<bool> fail_sync{false};
    std::atomic_bool throw_reads{false};
    std::atomic_bool throw_writes{false};
    std::atomic_bool throw_allocations{false};
    std::atomic_bool throw_page_counts{false};
    std::atomic_bool throw_sync_relation{false};
    std::atomic_bool throw_sync_all{false};
    std::atomic_bool throw_removals{false};

private:
    using Key = std::pair<std::uint32_t, std::uint32_t>;

    static auto key(const PageAddress address) noexcept -> Key {
        return {address.relation_id.value(), address.page_id.value()};
    }

    std::mutex pages_mutex_;
    std::map<Key, Page::Storage> pages_;
    std::map<std::uint32_t, std::uint32_t> next_page_;
    std::mutex read_mutex_;
    std::condition_variable read_changed_;
    bool block_reads_{false};
    std::size_t blocked_read_count_{0U};
    std::mutex write_mutex_;
    std::condition_variable write_changed_;
    bool block_writes_{false};
    bool write_started_{false};
    DurabilityTrace* trace_;
};

class FaultingReplacement final : public ReplacementPolicy {
public:
    void throw_next_evict() noexcept {
        throw_next_evict_.store(true, std::memory_order_release);
    }

    auto evict(const std::span<Frame* const> frames) -> base::Result<PinGuard> override {
        if (throw_next_evict_.exchange(false, std::memory_order_acq_rel)) {
            throw std::runtime_error{"injected replacement eviction exception"};
        }
        if (frames.size() != 1U || frames.front() == nullptr) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kInvalidArgument,
                    "faulting replacement expects exactly one frame",
                }
            );
        }
        return frames.front()->try_claim_evictable();
    }

    void on_pin(const base::FrameId frame_id) noexcept override {
        static_cast<void>(frame_id);
    }

    void on_remove(const base::FrameId frame_id) noexcept override {
        static_cast<void>(frame_id);
    }

private:
    std::atomic_bool throw_next_evict_{false};
};

class BlockingUnavailableReplacement final : public ReplacementPolicy {
public:
    auto evict(const std::span<Frame* const> frames) -> base::Result<PinGuard> override {
        static_cast<void>(frames);
        std::unique_lock lock{mutex_};
        entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this] {
            return released_;
        });
        return base::fail(
            base::Error{
                base::ErrorCode::kUnavailable,
                "injected replacement exhaustion",
            }
        );
    }

    void on_pin(const base::FrameId frame_id) noexcept override {
        static_cast<void>(frame_id);
    }

    void on_remove(const base::FrameId frame_id) noexcept override {
        static_cast<void>(frame_id);
    }

    void wait_until_entered() {
        std::unique_lock lock{mutex_};
        changed_.wait(lock, [this] {
            return entered_;
        });
    }

    void release() {
        const std::lock_guard lock{mutex_};
        released_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool entered_{false};
    bool released_{false};
};

struct PoolFixture final {
    explicit PoolFixture(const std::size_t size)
        : disk{std::make_unique<MockDiskManager>()},
          disk_view{disk.get()} {
        auto created = BufferPool::create(size, std::move(disk));
        EXPECT_TRUE(created);
        pool = std::move(created).value();
    }

    std::unique_ptr<MockDiskManager> disk;
    MockDiskManager* disk_view;
    std::unique_ptr<BufferPool> pool;
};

struct WorkingSetConfig final {
    std::size_t pages_per_thread;
    std::size_t iterations;
    std::size_t max_attempts;
    std::size_t marker_offset;
};

auto write_marker_with_retry(
    BufferPool& pool,
    const PageAddress address,
    const std::byte marker,
    const WorkingSetConfig& config
) -> bool {
    for (std::size_t attempt = 0; attempt < config.max_attempts; ++attempt) {
        auto page = pool.write_page(address);
        if (page) {
            auto destination = page.value().page()->mutable_view(config.marker_offset, 1);
            if (!destination) {
                return false;
            }
            destination.value().front() = marker;
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

auto read_marker_with_retry(
    BufferPool& pool,
    const PageAddress address,
    const std::byte marker,
    const WorkingSetConfig& config
) -> bool {
    for (std::size_t attempt = 0; attempt < config.max_attempts; ++attempt) {
        auto page = pool.read_page(address);
        if (page) {
            const auto source = page.value().page()->view(config.marker_offset, 1);
            return source && source.value().front() == marker;
        }
        std::this_thread::yield();
    }
    return false;
}

void exercise_working_set(
    BufferPool& pool,
    const std::vector<PageAddress>& addresses,
    std::atomic<bool>& start,
    std::atomic<std::size_t>& failures,
    const std::size_t thread,
    const WorkingSetConfig& config
) {
    while (!start.load()) {
        std::this_thread::yield();
    }
    const auto marker = static_cast<std::byte>(thread + 0x40U);
    for (std::size_t iteration = 0; iteration < config.iterations; ++iteration) {
        const auto address =
            addresses[thread * config.pages_per_thread + iteration % config.pages_per_thread];
        if (!write_marker_with_retry(pool, address, marker, config)
            || !read_marker_with_retry(pool, address, marker, config)) {
            failures.fetch_add(1);
            return;
        }
    }
}

TEST(BufferPoolTest, RepeatedFetchHitsCacheAndSharesFrame) {
    PoolFixture fixture{2};
    const auto address = fixture.disk_view->seed(base::RelationId{1}).value();
    {
        auto page = fixture.pool->read_page(address);
        ASSERT_TRUE(page);
        EXPECT_EQ(fixture.pool->frame_snapshot(address).value().pin_count, 1U);
    }
    {
        auto page = fixture.pool->read_page(address);
        ASSERT_TRUE(page);
    }
    EXPECT_EQ(fixture.disk_view->read_count.load(), 1U);
    EXPECT_TRUE(fixture.pool->contains(address));
}

TEST(BufferPoolTest, WriteGuardDirtiesAndFlushesPersistedImage) {
    PoolFixture fixture{2};
    const auto address = fixture.disk_view->seed(base::RelationId{2}).value();
    {
        auto page = fixture.pool->write_page(address);
        ASSERT_TRUE(page);
        auto byte = page.value().page()->mutable_view(100, 1);
        ASSERT_TRUE(byte);
        byte.value().front() = std::byte{0xA5};
    }
    EXPECT_TRUE(fixture.pool->frame_snapshot(address).value().dirty);
    const auto flushed = fixture.pool->flush_page(address);
    ASSERT_TRUE(flushed);
    EXPECT_TRUE(flushed.value());
    EXPECT_FALSE(fixture.pool->frame_snapshot(address).value().dirty);
    EXPECT_EQ(fixture.disk_view->persisted_byte(address, 100), std::byte{0xA5});
}

TEST(BufferPoolTest, DirtyEvictionWritesOldPageAndPublishesNewMapping) {
    PoolFixture fixture{1};
    const auto old_address = fixture.disk_view->seed(base::RelationId{3}).value();
    const auto new_address = fixture.disk_view->seed(base::RelationId{4}).value();
    {
        auto page = fixture.pool->write_page(old_address);
        ASSERT_TRUE(page);
        page.value().page()->mutable_view(120, 1).value().front() = std::byte{0xCC};
    }
    {
        auto page = fixture.pool->read_page(new_address);
        ASSERT_TRUE(page);
    }
    EXPECT_FALSE(fixture.pool->contains(old_address));
    EXPECT_TRUE(fixture.pool->contains(new_address));
    EXPECT_EQ(fixture.disk_view->persisted_byte(old_address, 120), std::byte{0xCC});
}

TEST(BufferPoolTest, LocalMappingHintNeverExposesARecycledFrame) {
    PoolFixture fixture{1};
    const auto old_address = fixture.disk_view->seed(base::RelationId{54U}).value();
    const auto new_address = fixture.disk_view->seed(base::RelationId{55U}).value();

    ASSERT_TRUE(fixture.pool->read_page(old_address));
    ASSERT_TRUE(fixture.pool->read_page(new_address));
    EXPECT_FALSE(fixture.pool->contains(old_address));

    auto old_page = fixture.pool->read_page(old_address);
    ASSERT_TRUE(old_page);
    EXPECT_FALSE(fixture.pool->contains(new_address));
    EXPECT_EQ(fixture.disk_view->read_count.load(), 3U);
}

TEST(BufferPoolTest, PinnedOnlyFrameReportsPoolExhaustion) {
    PoolFixture fixture{1};
    const auto first = fixture.disk_view->seed(base::RelationId{5}).value();
    const auto second = fixture.disk_view->seed(base::RelationId{6}).value();
    auto held = fixture.pool->read_page(first);
    ASSERT_TRUE(held);
    const auto exhausted = fixture.pool->read_page(second);
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code(), base::ErrorCode::kUnavailable);
}

TEST(BufferPoolTest, RechecksFreeStackAfterConcurrentRelationRetirement) {
    auto disk = std::make_unique<MockDiskManager>();
    auto* const disk_view = disk.get();
    const auto retired = disk_view->seed(base::RelationId{56U}).value();
    const auto requested = disk_view->seed(base::RelationId{57U}).value();
    auto replacement = std::make_unique<BlockingUnavailableReplacement>();
    auto* const replacement_view = replacement.get();
    auto created = BufferPool::create(1U, std::move(disk), std::move(replacement));
    ASSERT_TRUE(created);
    auto pool = std::move(created).value();
    ASSERT_TRUE(pool->read_page(retired));

    bool fetched{false};
    std::thread loader{[&] {
        fetched = static_cast<bool>(pool->read_page(requested));
    }};
    replacement_view->wait_until_entered();
    auto removed = pool->remove_relation(retired.relation_id);
    replacement_view->release();
    loader.join();

    ASSERT_TRUE(removed);
    EXPECT_TRUE(fetched);
    EXPECT_FALSE(pool->contains(retired));
    EXPECT_TRUE(pool->contains(requested));
}

TEST(BufferPoolTest, ConcurrentMissLoadsOnePageImage) {
    PoolFixture fixture{4};
    const auto address = fixture.disk_view->seed(base::RelationId{7}).value();
    fixture.disk_view->block_reads();
    std::atomic<bool> start{false};
    std::atomic<std::size_t> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(8U);
    for (std::size_t index = 0; index < 8; ++index) {
        threads.emplace_back([&] {
            while (!start.load()) {
                std::this_thread::yield();
            }
            auto page = fixture.pool->read_page(address);
            if (!page) {
                failures.fetch_add(1);
            }
        });
    }
    start.store(true);
    fixture.disk_view->wait_for_blocked_reads(1U);
    fixture.disk_view->release_reads();
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(failures.load(), 0U);
    EXPECT_EQ(fixture.disk_view->read_count.load(), 1U);
}

TEST(BufferPoolTest, DistinctMissesDoNotHoldAnAdmissionLockAcrossIo) {
    PoolFixture fixture{4U};
    const auto first_address = fixture.disk_view->seed(base::RelationId{56U}).value();
    const auto second_address = fixture.disk_view->seed(base::RelationId{56U}).value();
    fixture.disk_view->block_reads();

    std::atomic_bool first_ok{false};
    std::atomic_bool second_ok{false};
    std::thread first{[&] {
        first_ok.store(static_cast<bool>(fixture.pool->read_page(first_address)));
    }};
    std::thread second{[&] {
        second_ok.store(static_cast<bool>(fixture.pool->read_page(second_address)));
    }};

    fixture.disk_view->wait_for_blocked_reads(2U);
    fixture.disk_view->release_reads();
    first.join();
    second.join();

    EXPECT_TRUE(first_ok.load());
    EXPECT_TRUE(second_ok.load());
    EXPECT_EQ(fixture.disk_view->read_count.load(), 2U);
}

TEST(BufferPoolTest, FailedLoaderWakesSameAddressFollowersForOneRetry) {
    PoolFixture fixture{4U};
    const auto address = fixture.disk_view->seed(base::RelationId{57U}).value();
    fixture.disk_view->block_reads();
    fixture.disk_view->throw_reads.store(true, std::memory_order_release);

    std::atomic_bool leader_failed{false};
    std::thread leader{[&] {
        auto page = fixture.pool->read_page(address);
        leader_failed.store(!page && page.error().code() == base::ErrorCode::kInternal);
    }};
    fixture.disk_view->wait_for_blocked_reads(1U);

    constexpr std::size_t k_follower_count = 7U;
    std::atomic_size_t follower_failures{0U};
    std::vector<std::thread> followers;
    followers.reserve(k_follower_count);
    for (std::size_t index = 0U; index < k_follower_count; ++index) {
        followers.emplace_back([&] {
            if (!fixture.pool->read_page(address)) {
                follower_failures.fetch_add(1U);
            }
        });
    }

    fixture.disk_view->release_reads();
    leader.join();
    for (auto& follower : followers) {
        follower.join();
    }

    EXPECT_TRUE(leader_failed.load());
    EXPECT_EQ(follower_failures.load(), 0U);
    EXPECT_EQ(fixture.disk_view->read_count.load(), 2U);
}

TEST(BufferPoolTest, WritebackKeepsPageDirtyWhenExistingPinRedirtiesIt) {
    PoolFixture fixture{2};
    const auto address = fixture.disk_view->seed(base::RelationId{8}).value();
    {
        auto page = fixture.pool->write_page(address);
        ASSERT_TRUE(page);
        page.value().page()->mutable_view(140, 1).value().front() = std::byte{1};
    }
    auto writer_pin_result = fixture.pool->pin_page(address);
    ASSERT_TRUE(writer_pin_result);
    std::optional<PinGuard> writer_pin{std::move(writer_pin_result).value()};
    fixture.disk_view->block_writes();
    std::atomic<bool> flush_ok{false};
    std::thread flusher{[&] {
        const auto flushed = fixture.pool->flush_page(address);
        flush_ok.store(flushed && flushed.value());
    }};
    fixture.disk_view->wait_for_write();
    {
        auto page = writer_pin->write_latch();
        ASSERT_TRUE(page);
        page.value().page()->mutable_view(140, 1).value().front() = std::byte{2};
    }
    fixture.disk_view->release_writes();
    flusher.join();
    EXPECT_TRUE(flush_ok.load());
    EXPECT_TRUE(fixture.pool->frame_snapshot(address).value().dirty);
    writer_pin.reset();
    EXPECT_TRUE(fixture.pool->flush_page(address).value());
    EXPECT_EQ(fixture.disk_view->persisted_byte(address, 140), std::byte{2});
}

TEST(BufferPoolTest, FailedEvictionRestoresOldMappingAndDirtyState) {
    PoolFixture fixture{1};
    const auto old_address = fixture.disk_view->seed(base::RelationId{9}).value();
    const auto new_address = fixture.disk_view->seed(base::RelationId{10}).value();
    {
        auto page = fixture.pool->write_page(old_address);
        ASSERT_TRUE(page);
        page.value().page()->mutable_view(160, 1).value().front() = std::byte{0x5A};
    }
    fixture.disk_view->fail_writes.store(true);
    const auto failed = fixture.pool->read_page(new_address);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_TRUE(fixture.pool->contains(old_address));
    EXPECT_FALSE(fixture.pool->contains(new_address));
    EXPECT_TRUE(fixture.pool->frame_snapshot(old_address).value().dirty);
    fixture.disk_view->fail_writes.store(false);
    EXPECT_TRUE(fixture.pool->read_page(old_address));
}

TEST(BufferPoolTest, ShutdownRequiresReleasedGuardsThenFlushesAndCloses) {
    PoolFixture fixture{2};
    const auto address = fixture.disk_view->seed(base::RelationId{11}).value();
    auto held_result = fixture.pool->write_page(address);
    ASSERT_TRUE(held_result);
    std::optional<PageWriteGuard> held{std::move(held_result).value()};
    held->page()->mutable_view(180, 1).value().front() = std::byte{0x44};
    const auto busy = fixture.pool->shutdown();
    ASSERT_FALSE(busy);
    EXPECT_EQ(busy.error().code(), base::ErrorCode::kUnavailable);
    held.reset();
    EXPECT_TRUE(fixture.pool->shutdown());
    EXPECT_TRUE(fixture.pool->shutdown());
    EXPECT_TRUE(fixture.pool->is_shutdown());
    EXPECT_EQ(fixture.disk_view->persisted_byte(address, 180), std::byte{0x44});
    EXPECT_EQ(fixture.disk_view->sync_count.load(), 1U);
    EXPECT_FALSE(fixture.pool->read_page(address));
}

TEST(BufferPoolTest, AllocatesThroughDiskAndCachesInitializedPage) {
    PoolFixture fixture{2};
    auto pin = fixture.pool->allocate_page(base::RelationId{12}, PageKind::kBtree, 16);
    ASSERT_TRUE(pin);
    ASSERT_TRUE(pin.value().address().has_value());
    EXPECT_EQ(pin.value().address()->page_id, base::PageId{0});
    EXPECT_TRUE(fixture.pool->contains(*pin.value().address()));
    EXPECT_EQ(fixture.disk_view->read_count.load(), 1U);
}

TEST(BufferPoolTest, RemoveRelationRetiresCachedPagesAndRejectsPinnedPages) {
    PoolFixture fixture{3U};
    const base::RelationId relation{41U};
    const PageAddress first_address{relation, base::PageId{0U}};
    {
        auto first = fixture.pool->allocate_page(relation, PageKind::kHeap);
        ASSERT_TRUE(first);
        EXPECT_EQ(first->address(), first_address);
    }
    {
        auto second = fixture.pool->allocate_page(relation, PageKind::kHeap);
        ASSERT_TRUE(second);
    }
    const PageAddress second_address{relation, base::PageId{1U}};
    ASSERT_TRUE(fixture.pool->contains(first_address));
    ASSERT_TRUE(fixture.pool->contains(second_address));

    {
        auto pinned = fixture.pool->pin_page(first_address);
        ASSERT_TRUE(pinned);
        auto rejected = fixture.pool->remove_relation(relation);
        ASSERT_FALSE(rejected);
        EXPECT_EQ(rejected.error().code(), base::ErrorCode::kUnavailable);
        EXPECT_TRUE(fixture.pool->contains(first_address));
        EXPECT_TRUE(fixture.pool->contains(second_address));
    }

    ASSERT_TRUE(fixture.pool->remove_relation(relation));
    EXPECT_FALSE(fixture.pool->contains(first_address));
    auto count = fixture.pool->relation_page_count(relation);
    ASSERT_TRUE(count);
    EXPECT_EQ(count.value(), 0U);
}

TEST(BufferPoolTest, DiskPortExceptionsAreStructuredAndLeaveOperationsRetryable) {
    PoolFixture fixture{1U};
    const base::RelationId existing_relation{43U};
    const auto address = fixture.disk_view->seed(existing_relation).value();

    fixture.disk_view->throw_reads.store(true);
    auto read = fixture.pool->read_page(address);
    ASSERT_FALSE(read);
    EXPECT_EQ(read.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(read.error().message().find("injected mock read exception"), std::string_view::npos);
    EXPECT_FALSE(fixture.pool->contains(address));
    EXPECT_TRUE(fixture.pool->read_page(address));

    const base::RelationId new_relation{44U};
    fixture.disk_view->throw_allocations.store(true);
    auto allocated = fixture.pool->allocate_page(new_relation, PageKind::kHeap);
    ASSERT_FALSE(allocated);
    EXPECT_EQ(allocated.error().code(), base::ErrorCode::kResourceExhausted);
    EXPECT_EQ(fixture.disk_view->page_count(new_relation).value(), 0U);

    fixture.disk_view->throw_page_counts.store(true);
    auto counted = fixture.pool->relation_page_count(existing_relation);
    ASSERT_FALSE(counted);
    EXPECT_EQ(counted.error().code(), base::ErrorCode::kInternal);
    EXPECT_EQ(fixture.pool->relation_page_count(existing_relation).value(), 1U);

    fixture.disk_view->throw_sync_relation.store(true);
    auto relation_sync = fixture.pool->sync_relation(existing_relation);
    ASSERT_FALSE(relation_sync);
    EXPECT_EQ(relation_sync.error().code(), base::ErrorCode::kInternal);
    EXPECT_TRUE(fixture.pool->sync_relation(existing_relation));

    fixture.disk_view->throw_sync_all.store(true);
    auto global_sync = fixture.pool->sync_all();
    ASSERT_FALSE(global_sync);
    EXPECT_EQ(global_sync.error().code(), base::ErrorCode::kInternal);
    EXPECT_TRUE(fixture.pool->sync_all());
}

TEST(BufferPoolTest, DiskWriteExceptionFinishesIoAndPreservesDirtyRetryState) {
    PoolFixture fixture{1U};
    const auto address = fixture.disk_view->seed(base::RelationId{45U}).value();
    {
        auto page = fixture.pool->write_page(address);
        ASSERT_TRUE(page);
        page->page()->mutable_view(128U, 1U).value().front() = std::byte{0x5A};
    }

    fixture.disk_view->throw_writes.store(true);
    auto flushed = fixture.pool->flush_page(address);
    ASSERT_FALSE(flushed);
    EXPECT_EQ(flushed.error().code(), base::ErrorCode::kInternal);
    const auto failed_state = fixture.pool->frame_snapshot(address);
    ASSERT_TRUE(failed_state);
    EXPECT_TRUE(failed_state->dirty);
    EXPECT_FALSE(failed_state->io_in_progress);

    ASSERT_TRUE(fixture.pool->flush_page(address));
    EXPECT_EQ(fixture.disk_view->persisted_byte(address, 128U), std::byte{0x5A});
}

TEST(BufferPoolTest, DurabilityExceptionPreventsDataWriteAndPreservesDirtyRetryState) {
    DurabilityTrace trace;
    auto disk = std::make_unique<MockDiskManager>(&trace);
    auto* const disk_view = disk.get();
    const auto address = disk_view->seed(base::RelationId{46U}).value();
    auto created = BufferPool::create(
        1U,
        std::move(disk),
        nullptr,
        std::make_unique<RecordingDurability>(trace, false, true)
    );
    ASSERT_TRUE(created);
    auto pool = std::move(created).value();
    {
        auto page = pool->write_page(address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page->page()->set_lsn(base::Lsn{77U}));
    }

    auto flushed = pool->flush_page(address);
    ASSERT_FALSE(flushed);
    EXPECT_EQ(flushed.error().code(), base::ErrorCode::kInternal);
    EXPECT_EQ(disk_view->write_count.load(), 0U);
    const auto failed_state = pool->frame_snapshot(address);
    ASSERT_TRUE(failed_state);
    EXPECT_TRUE(failed_state->dirty);
    EXPECT_FALSE(failed_state->io_in_progress);

    ASSERT_TRUE(pool->flush_page(address));
    EXPECT_EQ(disk_view->write_count.load(), 1U);
}

TEST(BufferPoolTest, ReplacementEvictionExceptionsKeepVictimSelectionRetryable) {
    auto disk = std::make_unique<MockDiskManager>();
    auto* const disk_view = disk.get();
    const auto old_address = disk_view->seed(base::RelationId{47U}).value();
    const auto new_address = disk_view->seed(base::RelationId{48U}).value();
    auto replacement = std::make_unique<FaultingReplacement>();
    auto* const replacement_view = replacement.get();
    auto created = BufferPool::create(1U, std::move(disk), std::move(replacement));
    ASSERT_TRUE(created);
    auto pool = std::move(created).value();
    ASSERT_TRUE(pool->read_page(old_address));

    replacement_view->throw_next_evict();
    auto eviction_failed = pool->read_page(new_address);
    ASSERT_FALSE(eviction_failed);
    EXPECT_EQ(eviction_failed.error().code(), base::ErrorCode::kInternal);
    EXPECT_TRUE(pool->contains(old_address));
    EXPECT_FALSE(pool->contains(new_address));

    EXPECT_TRUE(pool->read_page(new_address));
    EXPECT_FALSE(pool->contains(old_address));
    EXPECT_TRUE(pool->contains(new_address));
}

TEST(BufferPoolTest, DiskRemovalExceptionLeavesRetiredCacheAndRetryableDurableRelation) {
    PoolFixture fixture{1U};
    const base::RelationId relation{51U};
    const PageAddress address{relation, base::PageId{0U}};
    {
        auto page = fixture.pool->allocate_page(relation, PageKind::kHeap);
        ASSERT_TRUE(page);
    }

    fixture.disk_view->throw_removals.store(true);
    auto removed = fixture.pool->remove_relation(relation);
    ASSERT_FALSE(removed);
    EXPECT_EQ(removed.error().code(), base::ErrorCode::kInternal);
    EXPECT_FALSE(fixture.pool->contains(address));
    EXPECT_EQ(fixture.disk_view->page_count(relation).value(), 1U);

    EXPECT_TRUE(fixture.pool->remove_relation(relation));
    EXPECT_EQ(fixture.disk_view->page_count(relation).value(), 0U);
}

TEST(BufferPoolTest, RejectsInvalidConstruction) {
    EXPECT_FALSE(BufferPool::create(0, std::make_unique<MockDiskManager>()));
    EXPECT_FALSE(BufferPool::create(1, nullptr));
}

TEST(BufferPoolTest, EvictionFlushesWalBeforeWritingLoggedPage) {
    DurabilityTrace trace;
    auto disk = std::make_unique<MockDiskManager>(&trace);
    auto* const disk_view = disk.get();
    const auto old_address = disk_view->seed(base::RelationId{13}).value();
    const auto new_address = disk_view->seed(base::RelationId{14}).value();
    auto created = BufferPool::create(
        1,
        std::move(disk),
        nullptr,
        std::make_unique<RecordingDurability>(trace)
    );
    ASSERT_TRUE(created);
    auto pool = std::move(created).value();
    {
        auto page = pool->write_page(old_address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page.value().page()->set_lsn(base::Lsn{42}));
    }
    ASSERT_TRUE(pool->read_page(new_address));
    EXPECT_EQ(
        trace.snapshot(),
        (std::vector<DurabilityEvent>{
            {DurabilityEventKind::kWal, base::Lsn{42}},
            {DurabilityEventKind::kData, base::Lsn{42}},
        })
    );
}

TEST(BufferPoolTest, LoggedPageRequiresDurabilityPort) {
    PoolFixture fixture{1};
    const auto address = fixture.disk_view->seed(base::RelationId{15}).value();
    {
        auto page = fixture.pool->write_page(address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page.value().page()->set_lsn(base::Lsn{7}));
    }
    const auto flushed = fixture.pool->flush_page(address);
    ASSERT_FALSE(flushed);
    EXPECT_EQ(flushed.error().code(), base::ErrorCode::kInvariantViolation);
    EXPECT_EQ(fixture.disk_view->write_count.load(), 0U);
    EXPECT_TRUE(fixture.pool->frame_snapshot(address).value().dirty);
}

TEST(BufferPoolTest, WalFlushFailurePreventsDataWriteAndPreservesDirtyPage) {
    DurabilityTrace trace;
    auto disk = std::make_unique<MockDiskManager>(&trace);
    auto* const disk_view = disk.get();
    const auto address = disk_view->seed(base::RelationId{16}).value();
    auto created = BufferPool::create(
        1,
        std::move(disk),
        nullptr,
        std::make_unique<RecordingDurability>(trace, true)
    );
    ASSERT_TRUE(created);
    auto pool = std::move(created).value();
    {
        auto page = pool->write_page(address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page.value().page()->set_lsn(base::Lsn{99}));
    }
    const auto flushed = pool->flush_page(address);
    ASSERT_FALSE(flushed);
    EXPECT_EQ(flushed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(disk_view->write_count.load(), 0U);
    EXPECT_TRUE(pool->frame_snapshot(address).value().dirty);
    EXPECT_EQ(
        trace.snapshot(),
        (std::vector<DurabilityEvent>{{DurabilityEventKind::kWal, base::Lsn{99}}})
    );
}

TEST(BufferPoolTest, UnloggedPageWritesWithoutFlushingWal) {
    DurabilityTrace trace;
    auto disk = std::make_unique<MockDiskManager>(&trace);
    auto* const disk_view = disk.get();
    const auto address = disk_view->seed(base::RelationId{17}).value();
    auto created = BufferPool::create(
        1,
        std::move(disk),
        nullptr,
        std::make_unique<RecordingDurability>(trace)
    );
    ASSERT_TRUE(created);
    auto pool = std::move(created).value();
    {
        auto page = pool->write_page(address);
        ASSERT_TRUE(page);
        page.value().page()->mutable_view(200, 1).value().front() = std::byte{0xA1};
    }
    ASSERT_TRUE(pool->flush_page(address));
    EXPECT_EQ(
        trace.snapshot(),
        (std::vector<DurabilityEvent>{
            {DurabilityEventKind::kData, base::kInvalidLsn},
        })
    );
}

TEST(BufferPoolTest, CheckpointDirtyBatchesBoundOneMaintenanceTurn) {
    PoolFixture fixture{4};
    std::vector<PageAddress> addresses;
    for (std::uint32_t index = 0; index < 4; ++index) {
        const auto address = fixture.disk_view->seed(base::RelationId{24}).value();
        addresses.push_back(address);
        auto page = fixture.pool->write_page(address);
        ASSERT_TRUE(page);
        page->page()->mutable_view(240, 1).value().front() = static_cast<std::byte>(index);
    }

    const auto snapshot = fixture.pool->mark_checkpoint_dirty_pages();
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->marked, 4U);
    EXPECT_EQ(snapshot->max_lsn, base::kInvalidLsn);

    std::size_t cursor = 0;
    const auto first = fixture.pool->flush_checkpoint_dirty_batch(cursor, 2, snapshot->max_lsn);
    ASSERT_TRUE(first);
    EXPECT_EQ(first.value(), 2U);
    EXPECT_EQ(cursor, 2U);
    EXPECT_EQ(
        std::count_if(
            addresses.begin(),
            addresses.end(),
            [&fixture](const PageAddress address) {
                return fixture.pool->frame_snapshot(address)->dirty;
            }
        ),
        2
    );

    const auto second = fixture.pool->flush_checkpoint_dirty_batch(cursor, 2, snapshot->max_lsn);
    ASSERT_TRUE(second);
    EXPECT_EQ(second.value(), 2U);
    EXPECT_EQ(cursor, 0U);
    for (const auto address : addresses) {
        const auto state = fixture.pool->frame_snapshot(address);
        ASSERT_TRUE(state);
        EXPECT_FALSE(state->dirty);
        EXPECT_FALSE(state->checkpoint_needed);
    }
}

TEST(BufferPoolTest, CheckpointBatchSkipsPagesNewerThanItsOpeningSnapshot) {
    DurabilityTrace trace;
    auto disk = std::make_unique<MockDiskManager>(&trace);
    auto* const disk_view = disk.get();
    const auto address = disk_view->seed(base::RelationId{25}).value();
    auto created = BufferPool::create(
        1,
        std::move(disk),
        nullptr,
        std::make_unique<RecordingDurability>(trace)
    );
    ASSERT_TRUE(created);
    auto pool = std::move(created).value();
    {
        auto page = pool->write_page(address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page->page()->set_lsn(base::Lsn{10}));
    }
    const auto snapshot = pool->mark_checkpoint_dirty_pages();
    ASSERT_TRUE(snapshot);
    ASSERT_EQ(snapshot->marked, 1U);
    ASSERT_EQ(snapshot->max_lsn, base::Lsn{10});
    {
        auto page = pool->write_page(address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page->page()->set_lsn(base::Lsn{20}));
    }

    std::size_t cursor = 0;
    const auto flushed = pool->flush_checkpoint_dirty_batch(cursor, 1, snapshot->max_lsn);
    ASSERT_TRUE(flushed);
    EXPECT_EQ(flushed.value(), 0U);
    const auto state = pool->frame_snapshot(address);
    ASSERT_TRUE(state);
    EXPECT_TRUE(state->dirty);
    EXPECT_FALSE(state->checkpoint_needed);
    EXPECT_TRUE(trace.snapshot().empty());
}

TEST(BufferPoolTest, CheckpointWriteLeavesConcurrentRedirtyForTheNextGeneration) {
    DurabilityTrace trace;
    auto disk = std::make_unique<MockDiskManager>(&trace);
    auto* const disk_view = disk.get();
    const auto address = disk_view->seed(base::RelationId{26}).value();
    auto created = BufferPool::create(
        1,
        std::move(disk),
        nullptr,
        std::make_unique<RecordingDurability>(trace)
    );
    ASSERT_TRUE(created);
    auto pool = std::move(created).value();
    {
        auto page = pool->write_page(address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page->page()->set_lsn(base::Lsn{10}));
        page->page()->mutable_view(260, 1).value().front() = std::byte{1};
    }
    const auto snapshot = pool->mark_checkpoint_dirty_pages();
    ASSERT_TRUE(snapshot);
    auto concurrent_pin = pool->pin_page(address);
    ASSERT_TRUE(concurrent_pin);
    disk_view->block_writes();

    std::atomic<bool> checkpoint_ok{false};
    std::thread checkpointer{[&] {
        std::size_t cursor = 0;
        const auto flushed = pool->flush_checkpoint_dirty_batch(cursor, 1, snapshot->max_lsn);
        checkpoint_ok.store(flushed && flushed.value() == 1U);
    }};
    disk_view->wait_for_write();
    {
        auto page = concurrent_pin->write_latch();
        ASSERT_TRUE(page);
        ASSERT_TRUE(page->page()->set_lsn(base::Lsn{20}));
        page->page()->mutable_view(260, 1).value().front() = std::byte{2};
    }
    disk_view->release_writes();
    checkpointer.join();

    EXPECT_TRUE(checkpoint_ok.load());
    const auto state = pool->frame_snapshot(address);
    ASSERT_TRUE(state);
    EXPECT_TRUE(state->dirty);
    EXPECT_FALSE(state->checkpoint_needed);
    EXPECT_EQ(disk_view->persisted_byte(address, 260), std::byte{1});
    EXPECT_EQ(
        trace.snapshot(),
        (std::vector<DurabilityEvent>{
            {DurabilityEventKind::kWal, base::Lsn{10}},
            {DurabilityEventKind::kData, base::Lsn{10}},
        })
    );
}

TEST(BufferPoolTest, FailedCheckpointWalFlushPreservesDirtySnapshotMembership) {
    DurabilityTrace trace;
    auto disk = std::make_unique<MockDiskManager>(&trace);
    auto* const disk_view = disk.get();
    const auto address = disk_view->seed(base::RelationId{27}).value();
    auto created = BufferPool::create(
        1,
        std::move(disk),
        nullptr,
        std::make_unique<RecordingDurability>(trace, true)
    );
    ASSERT_TRUE(created);
    auto pool = std::move(created).value();
    {
        auto page = pool->write_page(address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page->page()->set_lsn(base::Lsn{30}));
    }
    const auto snapshot = pool->mark_checkpoint_dirty_pages();
    ASSERT_TRUE(snapshot);

    std::size_t cursor = 0;
    const auto failed = pool->flush_checkpoint_dirty_batch(cursor, 1, snapshot->max_lsn);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    const auto state = pool->frame_snapshot(address);
    ASSERT_TRUE(state);
    EXPECT_TRUE(state->dirty);
    EXPECT_TRUE(state->checkpoint_needed);
    EXPECT_EQ(disk_view->write_count.load(), 0U);
}

TEST(BufferPoolTest, SequentialScanLargerThanPoolPreservesEveryPage) {
    constexpr std::size_t k_pool_size = 4;
    constexpr std::uint32_t k_page_count = 64;
    constexpr std::size_t k_marker_offset = 256;
    constexpr std::size_t k_scan_rounds = 2;
    PoolFixture fixture{k_pool_size};
    const base::RelationId relation_id{18};
    std::vector<PageAddress> addresses;
    addresses.reserve(k_page_count);
    for (std::uint32_t index = 0; index < k_page_count; ++index) {
        const auto address = fixture.disk_view->seed(relation_id);
        ASSERT_TRUE(address);
        addresses.push_back(address.value());
        auto page = fixture.pool->write_page(address.value());
        ASSERT_TRUE(page);
        auto marker = page.value().page()->mutable_view(k_marker_offset, 1);
        ASSERT_TRUE(marker);
        marker.value().front() = static_cast<std::byte>(index);
    }
    ASSERT_TRUE(fixture.pool->flush_all());

    for (std::size_t round = 0; round < k_scan_rounds; ++round) {
        for (std::uint32_t index = 0; index < k_page_count; ++index) {
            auto page = fixture.pool->read_page(addresses[index]);
            ASSERT_TRUE(page);
            auto marker = page.value().page()->view(k_marker_offset, 1);
            ASSERT_TRUE(marker);
            EXPECT_EQ(marker.value().front(), static_cast<std::byte>(index));
        }
    }

    EXPECT_EQ(fixture.pool->pool_size(), k_pool_size);
    EXPECT_GT(fixture.disk_view->read_count.load(), k_page_count);
    EXPECT_EQ(fixture.disk_view->write_count.load(), k_page_count);
}

TEST(BufferPoolTest, InjectedReadFailureDoesNotPublishMappingOrConsumeFrame) {
    PoolFixture fixture{1};
    const auto failed_address = fixture.disk_view->seed(base::RelationId{19}).value();
    const auto reusable_address = fixture.disk_view->seed(base::RelationId{20}).value();
    fixture.disk_view->fail_reads.store(true);

    const auto failed = fixture.pool->read_page(failed_address);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_FALSE(fixture.pool->contains(failed_address));

    fixture.disk_view->fail_reads.store(false);
    EXPECT_TRUE(fixture.pool->read_page(reusable_address));
    EXPECT_TRUE(fixture.pool->read_page(failed_address));
}

TEST(BufferPoolTest, InjectedAllocationFailureLeavesPageSequenceAndPoolReusable) {
    PoolFixture fixture{1};
    const base::RelationId relation_id{21};
    fixture.disk_view->fail_allocations.store(true);

    const auto failed = fixture.pool->allocate_page(relation_id, PageKind::kHeap);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(fixture.disk_view->page_count(relation_id).value(), 0U);

    fixture.disk_view->fail_allocations.store(false);
    auto allocated = fixture.pool->allocate_page(relation_id, PageKind::kHeap);
    ASSERT_TRUE(allocated);
    ASSERT_TRUE(allocated.value().address().has_value());
    EXPECT_EQ(allocated.value().address()->page_id, base::PageId{0});
}

TEST(BufferPoolTest, InjectedShutdownSyncFailureReopensPoolForRetry) {
    PoolFixture fixture{1};
    const auto address = fixture.disk_view->seed(base::RelationId{22}).value();
    EXPECT_TRUE(fixture.pool->read_page(address));
    fixture.disk_view->fail_sync.store(true);

    const auto failed = fixture.pool->shutdown();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_FALSE(fixture.pool->is_shutdown());
    EXPECT_TRUE(fixture.pool->read_page(address));

    fixture.disk_view->fail_sync.store(false);
    EXPECT_TRUE(fixture.pool->shutdown());
    EXPECT_TRUE(fixture.pool->is_shutdown());
}

TEST(BufferPoolTest, EscapedShutdownSyncExceptionReopensPoolForRetry) {
    PoolFixture fixture{1U};
    const auto address = fixture.disk_view->seed(base::RelationId{54U}).value();
    EXPECT_TRUE(fixture.pool->read_page(address));
    fixture.disk_view->throw_sync_all.store(true);

    auto failed = fixture.pool->shutdown();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kInternal);
    EXPECT_FALSE(fixture.pool->is_shutdown());
    EXPECT_TRUE(fixture.pool->read_page(address));

    EXPECT_TRUE(fixture.pool->shutdown());
    EXPECT_TRUE(fixture.pool->is_shutdown());
}

TEST(BufferPoolTest, ConcurrentWorkingSetsLargerThanPoolRemainConsistent) {
    constexpr std::size_t k_pool_size = 8;
    constexpr std::size_t k_thread_count = 8;
    constexpr WorkingSetConfig k_config{
        .pages_per_thread = 4,
        .iterations = 128,
        .max_attempts = 1'000,
        .marker_offset = 320,
    };
    PoolFixture fixture{k_pool_size};
    std::vector<PageAddress> addresses;
    addresses.reserve(k_thread_count * k_config.pages_per_thread);
    for (std::size_t index = 0; index < k_thread_count * k_config.pages_per_thread; ++index) {
        const auto address = fixture.disk_view->seed(base::RelationId{23});
        ASSERT_TRUE(address);
        addresses.push_back(address.value());
    }

    std::atomic<bool> start{false};
    std::atomic<std::size_t> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(k_thread_count);
    for (std::size_t thread = 0; thread < k_thread_count; ++thread) {
        threads.emplace_back([&, thread] {
            exercise_working_set(*fixture.pool, addresses, start, failures, thread, k_config);
        });
    }
    start.store(true);
    for (auto& thread : threads) {
        thread.join();
    }
    ASSERT_EQ(failures.load(), 0U);
    ASSERT_TRUE(fixture.pool->flush_all());
    for (std::size_t thread = 0; thread < k_thread_count; ++thread) {
        const auto expected = static_cast<std::byte>(thread + 0x40U);
        for (std::size_t offset = 0; offset < k_config.pages_per_thread; ++offset) {
            const auto address = addresses[thread * k_config.pages_per_thread + offset];
            EXPECT_EQ(fixture.disk_view->persisted_byte(address, k_config.marker_offset), expected);
        }
    }
}

}  // namespace
}  // namespace db::storage
