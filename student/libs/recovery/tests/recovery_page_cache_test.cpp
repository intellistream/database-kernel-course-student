#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/heap_wal.hpp"
#include "db/base/error.hpp"
#include "db/recovery/heap_startup_redo.hpp"
#include "db/recovery/heap_wal.hpp"
#include "db/recovery/recovery_page_cache.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/durability.hpp"
#include "db/storage/page.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

enum class EventKind : std::uint8_t {
    kWalFlush,
    kPageWrite,
    kRelationSync,
};

struct Event final {
    EventKind kind;
    base::RelationId relation_id{base::kInvalidRelationId};
    base::PageId page_id{base::kInvalidPageId};
    base::Lsn lsn{base::kInvalidLsn};

    auto operator==(const Event&) const noexcept -> bool = default;
};

class EventLog final {
public:
    void record(Event event) {
        events_.push_back(event);
    }

    auto events() const noexcept -> const std::vector<Event>& {
        return events_;
    }

private:
    std::vector<Event> events_;
};

class RecordingDurability final : public storage::PageDurabilityPort {
public:
    explicit RecordingDurability(EventLog& events) noexcept
        : events_{events} {}

    auto flush_through(const base::Lsn target_lsn) -> base::Status override {
        events_.record(
            Event{
                .kind = EventKind::kWalFlush,
                .lsn = target_lsn,
            }
        );
        return {};
    }

private:
    EventLog& events_;
};

class RecordingDisk final : public storage::DiskManager {
public:
    using Key = std::pair<std::uint32_t, std::uint32_t>;

    explicit RecordingDisk(EventLog& events) noexcept
        : events_{events} {}

    auto read_page(const storage::PageAddress address) -> base::Result<storage::Page> override {
        ++read_count_;
        if (fail_reads_) {
            return base::fail(base::Error{base::ErrorCode::kIo, "injected recovery read failure"});
        }
        const auto found = pages_.find(key(address));
        if (found == pages_.end()) {
            return base::fail(base::Error{base::ErrorCode::kNotFound, "recovery page is absent"});
        }
        return storage::Page::load(found->second, address);
    }

    auto write_page(const storage::PageAddress address, const storage::Page& page)
        -> base::Status override {
        if (fail_writes_ != 0U) {
            --fail_writes_;
            return base::fail(base::Error{base::ErrorCode::kIo, "injected recovery write failure"});
        }
        ++write_count_;
        events_.record(
            Event{
                .kind = EventKind::kPageWrite,
                .relation_id = address.relation_id,
                .page_id = address.page_id,
                .lsn = page.lsn(),
            }
        );
        pages_[key(address)] = page.bytes();
        return {};
    }

    auto allocate_page(
        const base::RelationId relation_id,
        const storage::PageKind kind,
        const std::size_t special_size = 0U
    ) -> base::Result<storage::PageAddress> override {
        auto& count = page_counts_[relation_id.value()];
        const storage::PageAddress address{relation_id, base::PageId{count}};
        auto page = storage::Page::create(kind, address, special_size);
        if (!page) {
            return base::fail(std::move(page).error());
        }
        pages_[key(address)] = page->bytes();
        ++count;
        return address;
    }

    auto page_count(const base::RelationId relation_id) -> base::Result<std::uint64_t> override {
        ++page_count_queries_;
        const auto found = page_counts_.find(relation_id.value());
        return found == page_counts_.end() ? 0U : found->second;
    }

    auto sync_relation(const base::RelationId relation_id) -> base::Status override {
        ++sync_count_;
        events_.record(
            Event{
                .kind = EventKind::kRelationSync,
                .relation_id = relation_id,
            }
        );
        if (fail_syncs_ != 0U) {
            --fail_syncs_;
            return base::fail(base::Error{base::ErrorCode::kIo, "injected recovery sync failure"});
        }
        return {};
    }

    auto sync_all() -> base::Status override {
        return {};
    }

    auto remove_relation(const base::RelationId relation_id, const bool missing_ok = false)
        -> base::Status override {
        static_cast<void>(missing_ok);
        page_counts_.erase(relation_id.value());
        for (auto iterator = pages_.begin(); iterator != pages_.end();) {
            if (iterator->first.first == relation_id.value()) {
                iterator = pages_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        return {};
    }

    auto page_lsn(const storage::PageAddress address) const -> base::Result<base::Lsn> {
        const auto found = pages_.find(key(address));
        if (found == pages_.end()) {
            return base::fail(base::Error{base::ErrorCode::kNotFound, "recovery page is absent"});
        }
        auto page = storage::Page::load(found->second, address);
        if (!page) {
            return base::fail(std::move(page).error());
        }
        return page->lsn();
    }

    auto relation_page_count(const base::RelationId relation_id) const -> std::uint64_t {
        const auto found = page_counts_.find(relation_id.value());
        return found == page_counts_.end() ? 0U : found->second;
    }

    void fail_reads() noexcept {
        fail_reads_ = true;
    }

    void fail_next_syncs(const std::size_t count) noexcept {
        fail_syncs_ = count;
    }

    void fail_next_writes(const std::size_t count) noexcept {
        fail_writes_ = count;
    }

    auto read_count() const noexcept -> std::size_t {
        return read_count_;
    }

    auto page_count_queries() const noexcept -> std::size_t {
        return page_count_queries_;
    }

    auto write_count() const noexcept -> std::size_t {
        return write_count_;
    }

    auto sync_count() const noexcept -> std::size_t {
        return sync_count_;
    }

private:
    static auto key(const storage::PageAddress address) noexcept -> Key {
        return {address.relation_id.value(), address.page_id.value()};
    }

    EventLog& events_;
    std::map<Key, storage::Page::Storage> pages_;
    std::map<std::uint32_t, std::uint32_t> page_counts_;
    std::size_t read_count_{0U};
    std::size_t page_count_queries_{0U};
    std::size_t write_count_{0U};
    std::size_t sync_count_{0U};
    std::size_t fail_writes_{0U};
    std::size_t fail_syncs_{0U};
    bool fail_reads_{false};
};

auto replacement_request(
    const base::RelationId relation_id,
    const base::PageId page_id,
    const storage::PageKind kind = storage::PageKind::kHeap
) -> StartupPageRequest {
    return StartupPageRequest{
        .address = storage::PageAddress{relation_id, page_id},
        .expected_kind = kind,
        .load_mode = StartupPageLoadMode::kSkipExisting,
    };
}

auto existing_request(
    const base::RelationId relation_id,
    const base::PageId page_id,
    const storage::PageKind kind = storage::PageKind::kHeap
) -> StartupPageRequest {
    return StartupPageRequest{
        .address = storage::PageAddress{relation_id, page_id},
        .expected_kind = kind,
        .load_mode = StartupPageLoadMode::kReadExisting,
    };
}

void mark_dirty(StartupPageLease& lease, const std::uint64_t lsn) {
    ASSERT_TRUE(lease.page().set_lsn(base::Lsn{lsn}));
    ASSERT_TRUE(lease.mark_dirty());
}

auto events_of_kind(const EventLog& events, const EventKind kind) -> std::vector<Event> {
    std::vector<Event> result;
    for (const auto& event : events.events()) {
        if (event.kind == kind) {
            result.push_back(event);
        }
    }
    return result;
}

TEST(RecoveryPageCacheTest, RejectsInvalidCapacityAndRequests) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};

    auto invalid_capacity =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 0U});
    ASSERT_FALSE(invalid_capacity);
    EXPECT_EQ(invalid_capacity.error().code(), base::ErrorCode::kInvalidArgument);

    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 1U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();
    auto invalid_address = recovery_cache.acquire(
        StartupPageRequest{
            .address = storage::PageAddress{base::kInvalidRelationId, base::PageId{0U}},
            .expected_kind = storage::PageKind::kHeap,
            .load_mode = StartupPageLoadMode::kSkipExisting,
        }
    );
    ASSERT_FALSE(invalid_address);
    EXPECT_EQ(invalid_address.error().code(), base::ErrorCode::kInvalidArgument);

    auto invalid_kind = recovery_cache.acquire(
        StartupPageRequest{
            .address = storage::PageAddress{base::RelationId{1U}, base::PageId{0U}},
            .expected_kind = storage::PageKind::kUninitialized,
            .load_mode = StartupPageLoadMode::kSkipExisting,
        }
    );
    ASSERT_FALSE(invalid_kind);
    EXPECT_EQ(invalid_kind.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_EQ(recovery_cache.cached_page_count(), 0U);
}

TEST(RecoveryPageCacheTest, RetainsHotPageAndWritesItOnceAtFinalDrain) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 1U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();

    const auto address = storage::PageAddress{base::RelationId{7U}, base::PageId{0U}};
    for (const auto lsn : {11U, 12U, 13U}) {
        auto lease =
            recovery_cache.acquire(replacement_request(address.relation_id, address.page_id));
        ASSERT_TRUE(lease) << lease.error().to_string();
        mark_dirty(*lease.value(), lsn);
    }

    EXPECT_EQ(
        recovery_cache.stats(),
        (RecoveryPageCacheStats{
            .page_reads = 0U,
            .page_writes = 0U,
            .cache_hits = 2U,
            .cache_misses = 1U,
            .evictions = 0U,
            .replacement_probes = 0U,
            .allocated_pages = 1U,
            .relation_syncs = 0U,
        })
    );
    ASSERT_TRUE(recovery_cache.drain_and_sync());
    EXPECT_TRUE(recovery_cache.is_finalized());
    EXPECT_EQ(recovery_cache.stats().page_writes, 1U);
    EXPECT_EQ(recovery_cache.stats().relation_syncs, 1U);
    EXPECT_EQ(disk.write_count(), 1U);
    EXPECT_EQ(disk.sync_count(), 1U);
    EXPECT_EQ(disk.page_lsn(address).value(), base::Lsn{13U});

    ASSERT_TRUE(recovery_cache.drain_and_sync());
    EXPECT_EQ(disk.write_count(), 1U);
    auto after_finalize =
        recovery_cache.acquire(replacement_request(address.relation_id, address.page_id));
    ASSERT_FALSE(after_finalize);
    EXPECT_EQ(after_finalize.error().code(), base::ErrorCode::kUnavailable);
}

TEST(RecoveryPageCacheTest, StreamsColdPagesThroughBoundedClockCache) {
    constexpr std::size_t capacity = 4U;
    constexpr std::uint32_t page_count = 19U;
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = capacity});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();

    const auto relation_id = base::RelationId{8U};
    for (std::uint32_t page_id = 0U; page_id < page_count; ++page_id) {
        auto lease =
            recovery_cache.acquire(replacement_request(relation_id, base::PageId{page_id}));
        ASSERT_TRUE(lease) << lease.error().to_string();
        mark_dirty(*lease.value(), static_cast<std::uint64_t>(page_id) + 1U);
    }
    ASSERT_TRUE(recovery_cache.drain_and_sync());

    const auto stats = recovery_cache.stats();
    EXPECT_EQ(stats.page_reads, 0U);
    EXPECT_EQ(stats.page_writes, page_count);
    EXPECT_EQ(stats.cache_hits, 0U);
    EXPECT_EQ(stats.cache_misses, page_count);
    EXPECT_EQ(stats.evictions, page_count - capacity);
    EXPECT_LE(stats.replacement_probes, stats.evictions * 2U + capacity);
    EXPECT_EQ(stats.allocated_pages, page_count);
    EXPECT_EQ(stats.relation_syncs, 1U);
    EXPECT_EQ(disk.page_count_queries(), 1U);
    EXPECT_EQ(disk.write_count(), page_count);
    EXPECT_EQ(disk.sync_count(), 1U);
    EXPECT_EQ(
        disk.page_lsn(storage::PageAddress{relation_id, base::PageId{page_count - 1U}}).value(),
        base::Lsn{page_count}
    );
}

TEST(RecoveryPageCacheTest, ClockGivesRecentlyReferencedPageASecondChance) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 3U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();

    const auto relation_id = base::RelationId{9U};
    for (std::uint32_t page_id = 0U; page_id < 4U; ++page_id) {
        auto lease =
            recovery_cache.acquire(replacement_request(relation_id, base::PageId{page_id}));
        ASSERT_TRUE(lease) << lease.error().to_string();
        mark_dirty(*lease.value(), static_cast<std::uint64_t>(page_id) + 1U);
    }

    auto hot = recovery_cache.acquire(replacement_request(relation_id, base::PageId{1U}));
    ASSERT_TRUE(hot) << hot.error().to_string();
    hot.value().reset();
    auto next = recovery_cache.acquire(replacement_request(relation_id, base::PageId{4U}));
    ASSERT_TRUE(next) << next.error().to_string();
    mark_dirty(*next.value(), 5U);

    EXPECT_TRUE(recovery_cache.contains(storage::PageAddress{relation_id, base::PageId{1U}}));
    EXPECT_FALSE(recovery_cache.contains(storage::PageAddress{relation_id, base::PageId{2U}}));
    EXPECT_TRUE(recovery_cache.contains(storage::PageAddress{relation_id, base::PageId{3U}}));
    EXPECT_TRUE(recovery_cache.contains(storage::PageAddress{relation_id, base::PageId{4U}}));
    next.value().reset();
    ASSERT_TRUE(recovery_cache.drain_and_sync());
}

TEST(RecoveryPageCacheTest, EvictionRetainsDirtyVictimAfterWriteFailure) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 1U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();

    const auto relation_id = base::RelationId{15U};
    const storage::PageAddress first{relation_id, base::PageId{0U}};
    const storage::PageAddress second{relation_id, base::PageId{1U}};
    auto first_lease = recovery_cache.acquire(replacement_request(relation_id, first.page_id));
    ASSERT_TRUE(first_lease) << first_lease.error().to_string();
    mark_dirty(*first_lease.value(), 15U);
    first_lease.value().reset();

    disk.fail_next_writes(1U);
    auto failed_replacement =
        recovery_cache.acquire(replacement_request(relation_id, second.page_id));
    ASSERT_FALSE(failed_replacement);
    EXPECT_EQ(failed_replacement.error().code(), base::ErrorCode::kIo);
    EXPECT_TRUE(recovery_cache.contains(first));
    EXPECT_FALSE(recovery_cache.contains(second));
    EXPECT_EQ(recovery_cache.stats().page_writes, 0U);
    EXPECT_EQ(disk.relation_page_count(relation_id), 2U);

    auto second_lease = recovery_cache.acquire(replacement_request(relation_id, second.page_id));
    ASSERT_TRUE(second_lease) << second_lease.error().to_string();
    mark_dirty(*second_lease.value(), 16U);
    second_lease.value().reset();
    ASSERT_TRUE(recovery_cache.drain_and_sync());

    EXPECT_EQ(recovery_cache.stats().page_writes, 2U);
    EXPECT_EQ(recovery_cache.stats().evictions, 1U);
    EXPECT_EQ(recovery_cache.stats().relation_syncs, 1U);
    EXPECT_EQ(disk.page_lsn(first).value(), base::Lsn{15U});
    EXPECT_EQ(disk.page_lsn(second).value(), base::Lsn{16U});
}

TEST(RecoveryPageCacheTest, SkipsUnreadableOldPageAndExtendsOrdinaryRedoTarget) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 2U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();

    const auto relation_id = base::RelationId{10U};
    disk.fail_reads();
    auto replacement = recovery_cache.acquire(replacement_request(relation_id, base::PageId{0U}));
    ASSERT_TRUE(replacement) << replacement.error().to_string();
    mark_dirty(*replacement.value(), 10U);
    EXPECT_EQ(disk.read_count(), 0U);

    auto missing_delta = recovery_cache.acquire(existing_request(relation_id, base::PageId{1U}));
    ASSERT_FALSE(missing_delta);
    EXPECT_EQ(missing_delta.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(disk.relation_page_count(relation_id), 2U);
    EXPECT_EQ(disk.read_count(), 1U);
    replacement.value().reset();
    ASSERT_TRUE(recovery_cache.drain_and_sync());
}

TEST(RecoveryPageCacheTest, ExtendsRelationThroughFullPageImageTarget) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 2U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();

    const auto relation_id = base::RelationId{14U};
    const auto target = storage::PageAddress{relation_id, base::PageId{2U}};
    auto image_target = recovery_cache.acquire(replacement_request(relation_id, target.page_id));
    ASSERT_TRUE(image_target) << image_target.error().to_string();
    mark_dirty(*image_target.value(), 14U);
    image_target.value().reset();

    EXPECT_EQ(disk.relation_page_count(relation_id), 3U);
    EXPECT_EQ(disk.read_count(), 0U);
    auto middle = recovery_cache.acquire(existing_request(relation_id, base::PageId{1U}));
    ASSERT_TRUE(middle) << middle.error().to_string();
    EXPECT_EQ(disk.read_count(), 1U);
    middle.value().reset();

    ASSERT_TRUE(recovery_cache.drain_and_sync());
    EXPECT_EQ(disk.write_count(), 1U);
    EXPECT_EQ(disk.sync_count(), 1U);
    auto target_lsn = disk.page_lsn(target);
    ASSERT_TRUE(target_lsn) << target_lsn.error().to_string();
    EXPECT_EQ(target_lsn.value(), base::Lsn{14U});
}

TEST(RecoveryPageCacheTest, ServesHeapStartupRedoAndMakesItDurable) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 1U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();

    const storage::PageAddress address{base::RelationId{13U}, base::PageId{0U}};
    auto payload = encode_heap_wal_payload(
        HeapWalPayload{
            .operation = access::HeapWalOperation::kInitialize,
            .page_flags = 0U,
            .tuples = {},
            .redirects = {},
            .dead = {},
            .unused = {},
        }
    );
    ASSERT_TRUE(payload) << payload.error().to_string();
    const wal::DecodedWalRecord record{
        .start_lsn = base::Lsn{700U},
        .end_lsn = base::Lsn{760U},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{13U},
            .resource_manager = wal::WalResourceManager::kHeap,
            .info = wal::opcode::heap::kInitialize,
            .blocks = {wal::WalBlockReference{
                .relation_id = address.relation_id,
                .page_id = address.page_id,
                .will_initialize = true,
                .standard_page = true,
                .full_page_image = std::nullopt,
                .delta = std::move(payload).value(),
            }},
            .main_payload = {},
        },
    };

    auto applied = replay_heap_startup_redo(record, recovery_cache);
    ASSERT_TRUE(applied) << applied.error().to_string();
    EXPECT_EQ(applied.value(), HeapRedoDisposition::kApplied);
    EXPECT_EQ(recovery_cache.stats().cache_misses, 1U);
    EXPECT_EQ(recovery_cache.stats().allocated_pages, 1U);
    EXPECT_EQ(disk.write_count(), 0U);

    ASSERT_TRUE(recovery_cache.drain_and_sync());
    auto lsn = disk.page_lsn(address);
    ASSERT_TRUE(lsn) << lsn.error().to_string();
    EXPECT_EQ(lsn.value(), record.end_lsn);
    EXPECT_EQ(disk.write_count(), 1U);
    EXPECT_EQ(disk.sync_count(), 1U);
}

TEST(RecoveryPageCacheTest, ReadsExistingPagesAndRejectsRouteKindChanges) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    const auto relation_id = base::RelationId{11U};
    const auto address = storage::PageAddress{relation_id, base::PageId{0U}};
    ASSERT_TRUE(disk.allocate_page(relation_id, storage::PageKind::kHeap));

    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 1U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();
    auto existing = recovery_cache.acquire(existing_request(relation_id, address.page_id));
    ASSERT_TRUE(existing) << existing.error().to_string();
    auto header = existing.value()->page().header();
    ASSERT_TRUE(header) << header.error().to_string();
    EXPECT_EQ(header->kind, storage::PageKind::kHeap);
    existing.value().reset();
    EXPECT_EQ(disk.read_count(), 1U);
    EXPECT_TRUE(recovery_cache.contains(address));

    auto wrong_kind = recovery_cache.acquire(
        replacement_request(relation_id, address.page_id, storage::PageKind::kBtree)
    );
    ASSERT_FALSE(wrong_kind);
    EXPECT_EQ(wrong_kind.error().code(), base::ErrorCode::kCorruption);
}

TEST(RecoveryPageCacheTest, DrainOrdersWritebackAndRetriesOnlyPendingSync) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 4U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();

    const std::vector<std::pair<storage::PageAddress, std::uint64_t>> pages{
        {{base::RelationId{2U}, base::PageId{0U}}, 30U},
        {{base::RelationId{1U}, base::PageId{0U}}, 10U},
        {{base::RelationId{2U}, base::PageId{1U}}, 40U},
        {{base::RelationId{1U}, base::PageId{1U}}, 20U},
    };
    for (const auto& [address, lsn] : pages) {
        auto lease =
            recovery_cache.acquire(replacement_request(address.relation_id, address.page_id));
        ASSERT_TRUE(lease) << lease.error().to_string();
        mark_dirty(*lease.value(), lsn);
    }

    disk.fail_next_syncs(1U);
    auto first_drain = recovery_cache.drain_and_sync();
    ASSERT_FALSE(first_drain);
    EXPECT_EQ(first_drain.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(recovery_cache.stats().page_writes, 4U);
    EXPECT_EQ(recovery_cache.stats().relation_syncs, 0U);
    EXPECT_FALSE(recovery_cache.is_finalized());

    ASSERT_TRUE(recovery_cache.drain_and_sync());
    EXPECT_TRUE(recovery_cache.is_finalized());
    EXPECT_EQ(recovery_cache.stats().page_writes, 4U);
    EXPECT_EQ(recovery_cache.stats().relation_syncs, 2U);

    const auto writes = events_of_kind(events, EventKind::kPageWrite);
    EXPECT_EQ(
        writes,
        (std::vector<Event>{
            Event{
                .kind = EventKind::kPageWrite,
                .relation_id = base::RelationId{1U},
                .page_id = base::PageId{0U},
                .lsn = base::Lsn{10U}
            },
            Event{
                .kind = EventKind::kPageWrite,
                .relation_id = base::RelationId{1U},
                .page_id = base::PageId{1U},
                .lsn = base::Lsn{20U}
            },
            Event{
                .kind = EventKind::kPageWrite,
                .relation_id = base::RelationId{2U},
                .page_id = base::PageId{0U},
                .lsn = base::Lsn{30U}
            },
            Event{
                .kind = EventKind::kPageWrite,
                .relation_id = base::RelationId{2U},
                .page_id = base::PageId{1U},
                .lsn = base::Lsn{40U}
            },
        })
    );
    const auto syncs = events_of_kind(events, EventKind::kRelationSync);
    EXPECT_EQ(
        syncs,
        (std::vector<Event>{
            Event{.kind = EventKind::kRelationSync, .relation_id = base::RelationId{1U}},
            Event{.kind = EventKind::kRelationSync, .relation_id = base::RelationId{1U}},
            Event{.kind = EventKind::kRelationSync, .relation_id = base::RelationId{2U}},
        })
    );

    const auto& all_events = events.events();
    for (std::size_t index = 0U; index < all_events.size(); ++index) {
        if (all_events[index].kind != EventKind::kPageWrite) {
            continue;
        }
        ASSERT_NE(index, 0U);
        EXPECT_EQ(all_events[index - 1U].kind, EventKind::kWalFlush);
        EXPECT_EQ(all_events[index - 1U].lsn, all_events[index].lsn);
    }
}

TEST(RecoveryPageCacheTest, FinalDrainRequiresReleasedLeasesAndDiscardsUnmarkedReplacement) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 1U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();

    const auto address = storage::PageAddress{base::RelationId{12U}, base::PageId{0U}};
    auto lease = recovery_cache.acquire(replacement_request(address.relation_id, address.page_id));
    ASSERT_TRUE(lease) << lease.error().to_string();
    auto blocked_acquire =
        recovery_cache.acquire(replacement_request(address.relation_id, base::PageId{1U}));
    ASSERT_FALSE(blocked_acquire);
    EXPECT_EQ(blocked_acquire.error().code(), base::ErrorCode::kUnavailable);
    EXPECT_EQ(disk.relation_page_count(address.relation_id), 1U);
    auto blocked = recovery_cache.drain_and_sync();
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code(), base::ErrorCode::kUnavailable);

    lease.value().reset();
    EXPECT_FALSE(recovery_cache.contains(address));
    EXPECT_EQ(recovery_cache.cached_page_count(), 0U);
    ASSERT_TRUE(recovery_cache.drain_and_sync());
    EXPECT_EQ(recovery_cache.stats().page_writes, 0U);
    EXPECT_EQ(recovery_cache.stats().relation_syncs, 1U);
}

TEST(RecoveryPageCacheTest, AbandonForRetryRequiresNoLeaseAndNeverWritesDirtyCandidates) {
    EventLog events;
    RecordingDisk disk{events};
    RecordingDurability durability{events};
    auto cache =
        RecoveryPageCache::create(disk, durability, RecoveryPageCacheOptions{.capacity = 1U});
    ASSERT_TRUE(cache) << cache.error().to_string();
    auto& recovery_cache = *cache.value();

    const auto address = storage::PageAddress{base::RelationId{13U}, base::PageId{0U}};
    auto lease = recovery_cache.acquire(replacement_request(address.relation_id, address.page_id));
    ASSERT_TRUE(lease) << lease.error().to_string();
    mark_dirty(*lease.value(), 99U);

    auto blocked = recovery_cache.abandon_for_retry();
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code(), base::ErrorCode::kUnavailable);

    lease.value().reset();
    ASSERT_TRUE(recovery_cache.abandon_for_retry());
    EXPECT_TRUE(recovery_cache.is_abandoned());
    EXPECT_FALSE(recovery_cache.is_finalized());
    EXPECT_EQ(recovery_cache.cached_page_count(), 0U);
    EXPECT_EQ(disk.write_count(), 0U);
    EXPECT_EQ(disk.sync_count(), 0U);

    auto drain = recovery_cache.drain_and_sync();
    ASSERT_FALSE(drain);
    EXPECT_EQ(drain.error().code(), base::ErrorCode::kUnavailable);
    auto reacquire =
        recovery_cache.acquire(replacement_request(address.relation_id, address.page_id));
    ASSERT_FALSE(reacquire);
    EXPECT_EQ(reacquire.error().code(), base::ErrorCode::kUnavailable);
}

}  // namespace
}  // namespace db::recovery
