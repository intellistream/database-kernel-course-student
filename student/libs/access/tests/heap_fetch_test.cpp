#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/heap_fetch.hpp"
#include "db/access/heap_page.hpp"
#include "db/access/heap_table.hpp"
#include "db/access/heap_wal.hpp"
#include "db/base/error.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/storage/durability.hpp"
#include "db/storage/page.hpp"
#include "db/storage/replacement.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/ssi.hpp"

namespace db::access {
namespace {

class MemoryDisk final : public storage::DiskManager {
public:
    auto read_page(const storage::PageAddress address) -> base::Result<storage::Page> override {
        const std::lock_guard lock{mutex_};
        const auto found = pages_.find(key(address));
        if (found == pages_.end()) {
            return base::fail(base::Error{base::ErrorCode::kNotFound, "heap fetch page not found"});
        }
        return storage::Page::load(found->second, address);
    }

    auto write_page(const storage::PageAddress address, const storage::Page& page)
        -> base::Status override {
        const std::lock_guard lock{mutex_};
        pages_[key(address)] = page.bytes();
        return {};
    }

    auto allocate_page(
        const base::RelationId relation_id,
        const storage::PageKind kind,
        const std::size_t special_size = 0
    ) -> base::Result<storage::PageAddress> override {
        const std::lock_guard lock{mutex_};
        auto& next = next_page_[relation_id.value()];
        const storage::PageAddress address{relation_id, base::PageId{next}};
        auto page = storage::Page::create(kind, address, special_size);
        if (!page) {
            return base::fail(std::move(page).error());
        }
        pages_[key(address)] = page->bytes();
        ++next;
        return address;
    }

    auto page_count(const base::RelationId relation_id) -> base::Result<std::uint64_t> override {
        const std::lock_guard lock{mutex_};
        const auto found = next_page_.find(relation_id.value());
        return found == next_page_.end() ? 0U : found->second;
    }

    auto sync_relation(base::RelationId relation_id) -> base::Status override {
        static_cast<void>(relation_id);
        return {};
    }

    auto sync_all() -> base::Status override {
        return {};
    }

    auto remove_relation(const base::RelationId relation_id, const bool missing_ok)
        -> base::Status override {
        const std::lock_guard lock{mutex_};
        const auto removed = next_page_.erase(relation_id.value());
        for (auto iterator = pages_.begin(); iterator != pages_.end();) {
            if (iterator->first.first == relation_id.value()) {
                iterator = pages_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        if (removed == 0U && !missing_ok) {
            return base::fail(
                base::Error{base::ErrorCode::kNotFound, "heap fetch relation not found"}
            );
        }
        return {};
    }

private:
    using PageKey = std::pair<std::uint32_t, std::uint32_t>;

    static auto key(const storage::PageAddress address) noexcept -> PageKey {
        return {address.relation_id.value(), address.page_id.value()};
    }

    std::mutex mutex_;
    std::map<PageKey, storage::Page::Storage> pages_;
    std::map<std::uint32_t, std::uint32_t> next_page_;
};

class AcceptDurability final : public storage::PageDurabilityPort {
public:
    auto flush_through(const base::Lsn target_lsn) -> base::Status override {
        if (target_lsn == base::kInvalidLsn) {
            return base::fail(
                base::Error{base::ErrorCode::kInvalidArgument, "invalid durability target"}
            );
        }
        return {};
    }
};

class MonotonicHeapLog final : public HeapMutationLogPort {
public:
    auto log(const HeapWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.after_page == nullptr) {
            return base::fail(
                base::Error{base::ErrorCode::kInvalidArgument, "missing heap after-image"}
            );
        }
        const auto result = base::Lsn{next_lsn_};
        next_lsn_ += 8U;
        return result;
    }

private:
    std::uint64_t next_lsn_{8};
};

class CountingReplacement final : public storage::ReplacementPolicy {
public:
    explicit CountingReplacement(const std::size_t pool_size)
        : delegate_{std::move(storage::LruReplacer::create(pool_size)).value()} {}

    auto evict(const std::span<storage::Frame* const> frames)
        -> base::Result<storage::PinGuard> override {
        return delegate_->evict(frames);
    }

    void on_pin(const base::FrameId frame_id) noexcept override {
        pin_count_.fetch_add(1, std::memory_order_relaxed);
        delegate_->on_pin(frame_id);
    }

    void reset() noexcept {
        pin_count_.store(0, std::memory_order_relaxed);
    }

    auto pin_count() const noexcept -> std::size_t {
        return pin_count_.load(std::memory_order_relaxed);
    }

private:
    std::unique_ptr<storage::LruReplacer> delegate_;
    std::atomic<std::size_t> pin_count_{0};
};

struct PoolBundle final {
    std::unique_ptr<storage::BufferPool> pool;
    CountingReplacement* replacement;
};

auto make_pool(const std::size_t frames = 4) -> PoolBundle {
    auto replacement = std::make_unique<CountingReplacement>(frames);
    auto* const replacement_view = replacement.get();
    auto pool = storage::BufferPool::create(
        frames,
        std::make_unique<MemoryDisk>(),
        std::move(replacement),
        std::make_unique<AcceptDurability>()
    );
    EXPECT_TRUE(pool) << pool.error().to_string();
    return PoolBundle{.pool = std::move(pool).value(), .replacement = replacement_view};
}

auto bytes(const std::string& text) -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const auto character : text) {
        result.push_back(static_cast<std::byte>(character));
    }
    return result;
}

auto text(const std::vector<std::byte>& payload) -> std::string {
    std::string result;
    result.reserve(payload.size());
    for (const auto value : payload) {
        result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return result;
}

void set_status(
    transaction::CommitLog& commit_log,
    const base::Xid xid,
    const transaction::TransactionStatus status
) {
    ASSERT_TRUE(commit_log.ensure_xid_allocated(xid));
    ASSERT_TRUE(commit_log.set_status(xid, status));
}

auto snapshot(
    const std::uint32_t xmin,
    const std::uint32_t xmax,
    std::vector<base::Xid> in_progress = {},
    const std::uint32_t command_id = 1
) -> transaction::Snapshot {
    return transaction::Snapshot{
        .xmin = base::Xid{xmin},
        .xmax = base::Xid{xmax},
        .in_progress = std::move(in_progress),
        .command_id = command_id,
    };
}

auto fetch_context(
    const base::Xid own_xid,
    const transaction::Snapshot& captured,
    const transaction::CommitLog& commit_log,
    transaction::SerializableConflictManager* const serializable_conflicts = nullptr
) noexcept -> HeapFetchContext {
    return HeapFetchContext{
        .mvcc = HeapMvccContext{
            .own_xid = own_xid,
            .snapshot = &captured,
            .commit_log = &commit_log,
            .serializable_conflicts = serializable_conflicts,
        },
    };
}

template <typename Change>
void rewrite_header(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const base::Rid rid,
    Change change
) {
    const storage::PageAddress address{relation_id, rid.page_id()};
    auto write = pool.write_page(address);
    ASSERT_TRUE(write);
    auto page = HeapPage::open(*write->page(), address);
    ASSERT_TRUE(page);
    auto tuple = page->mutable_tuple_bytes(rid.slot_id());
    ASSERT_TRUE(tuple);
    auto header = TupleHeader::decode(tuple->first(kTupleHeaderSize));
    ASSERT_TRUE(header);
    change(header.value());
    ASSERT_TRUE(header->encode(tuple->first(kTupleHeaderSize)));
}

void install_hot_link(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const base::Rid predecessor,
    const base::Rid successor,
    const base::Xid updater
) {
    rewrite_header(pool, relation_id, predecessor, [&](TupleHeader& header) {
        header.xmax = updater;
        header.ctid = successor;
        header.flags = tuple_flags::kUpdated | tuple_flags::kHotUpdated;
    });
    rewrite_header(pool, relation_id, successor, [](TupleHeader& header) {
        header.flags |= tuple_flags::kHeapOnly;
    });
}

TEST(HeapFetchTest, ReturnsOwnedTupleAndUsesExactlyOnePagePin) {
    auto bundle = make_pool();
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{101};
    auto table = HeapTable::create(relation_id, *bundle.pool, registry, heap_log).value();
    const auto rid = table.insert(base::Xid{10}, 0, bytes("alpha")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(11, 30);

    bundle.replacement->reset();
    auto fetched = table.fetch(rid, fetch_context(base::Xid{25}, captured, commit_log));
    ASSERT_TRUE(fetched);
    ASSERT_TRUE(fetched->has_value());
    EXPECT_EQ(fetched->value().rid, rid);
    EXPECT_EQ(text(fetched->value().payload), "alpha");
    EXPECT_EQ(bundle.replacement->pin_count(), 1U);
    EXPECT_EQ(
        bundle.pool->frame_snapshot(storage::PageAddress{relation_id, rid.page_id()})->pin_count,
        0U
    );

    rewrite_header(*bundle.pool, relation_id, rid, [](TupleHeader& header) {
        header.command_id = 99;
    });
    EXPECT_EQ(fetched->value().header.command_id, 0U);
    EXPECT_EQ(text(fetched->value().payload), "alpha");
}

TEST(HeapFetchTest, RejectsInvalidRidAndMissingVisibilityServices) {
    auto bundle = make_pool();
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{108}, *bundle.pool, registry, heap_log).value();
    const auto rid = table.insert(base::Xid{10}, 0, bytes("row")).value();
    transaction::CommitLog commit_log;
    const auto captured = snapshot(11, 30);

    auto invalid_rid = table.fetch(
        base::Rid{base::kInvalidPageId, base::SlotId{0}},
        fetch_context(base::Xid{25}, captured, commit_log)
    );
    ASSERT_FALSE(invalid_rid);
    EXPECT_EQ(invalid_rid.error().code(), base::ErrorCode::kInvalidArgument);

    auto invalid_xid = table.fetch(rid, fetch_context(base::kInvalidXid, captured, commit_log));
    ASSERT_FALSE(invalid_xid);
    EXPECT_EQ(invalid_xid.error().code(), base::ErrorCode::kInvalidArgument);

    auto missing_snapshot = table.fetch(
        rid,
        HeapFetchContext{
            .mvcc = HeapMvccContext{
                .own_xid = base::Xid{25},
                .snapshot = nullptr,
                .commit_log = &commit_log,
            },
        }
    );
    ASSERT_FALSE(missing_snapshot);
    EXPECT_EQ(missing_snapshot.error().code(), base::ErrorCode::kInvalidArgument);
}

TEST(HeapFetchTest, TraversesHotVersionsUnderOnePinAndHonorsSnapshotAge) {
    auto bundle = make_pool();
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{102};
    auto table = HeapTable::create(relation_id, *bundle.pool, registry, heap_log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("old")).value();
    const auto successor = table.insert(base::Xid{20}, 0, bytes("new")).value();
    install_hot_link(*bundle.pool, relation_id, root, successor, base::Xid{20});
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);

    const auto old_snapshot = snapshot(20, 21, {base::Xid{20}});
    bundle.replacement->reset();
    auto old = table.fetch(root, fetch_context(base::Xid{30}, old_snapshot, commit_log));
    ASSERT_TRUE(old);
    ASSERT_TRUE(old->has_value());
    EXPECT_EQ(old->value().rid, root);
    EXPECT_EQ(text(old->value().payload), "old");
    EXPECT_EQ(bundle.replacement->pin_count(), 1U);

    const auto new_snapshot = snapshot(21, 31);
    bundle.replacement->reset();
    auto current = table.fetch(root, fetch_context(base::Xid{30}, new_snapshot, commit_log));
    ASSERT_TRUE(current);
    ASSERT_TRUE(current->has_value());
    EXPECT_EQ(current->value().rid, successor);
    EXPECT_EQ(text(current->value().payload), "new");
    EXPECT_EQ(bundle.replacement->pin_count(), 1U);
}

TEST(HeapFetchTest, ResolvesAPrunedRedirectBeforeSearchingTheHotChain) {
    auto bundle = make_pool();
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{103};
    auto table = HeapTable::create(relation_id, *bundle.pool, registry, heap_log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("old")).value();
    const auto successor = table.insert(base::Xid{20}, 0, bytes("new")).value();
    rewrite_header(*bundle.pool, relation_id, successor, [](TupleHeader& header) {
        header.flags |= tuple_flags::kHeapOnly;
    });
    {
        const storage::PageAddress address{relation_id, root.page_id()};
        auto write = bundle.pool->write_page(address);
        ASSERT_TRUE(write);
        auto page = HeapPage::open(*write->page(), address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page->redirect(root.slot_id(), successor.slot_id()));
    }
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(21, 40);

    auto fetched = table.fetch(root, fetch_context(base::Xid{30}, captured, commit_log));
    ASSERT_TRUE(fetched);
    ASSERT_TRUE(fetched->has_value());
    EXPECT_EQ(fetched->value().rid, successor);
    EXPECT_EQ(text(fetched->value().payload), "new");
}

TEST(HeapFetchTest, IndexCandidateRecheckRejectsStaleDeadAndColdVersions) {
    auto bundle = make_pool();
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{104};
    auto table = HeapTable::create(relation_id, *bundle.pool, registry, heap_log).value();
    const auto cold_root = table.insert(base::Xid{10}, 0, bytes("old")).value();
    const auto cold_successor = table.insert(base::Xid{20}, 0, bytes("new")).value();
    const auto dead = table.insert(base::Xid{10}, 0, bytes("dead")).value();
    rewrite_header(*bundle.pool, relation_id, cold_root, [&](TupleHeader& header) {
        header.xmax = base::Xid{20};
        header.ctid = cold_successor;
        header.flags = tuple_flags::kUpdated;
    });
    {
        const storage::PageAddress address{relation_id, dead.page_id()};
        auto write = bundle.pool->write_page(address);
        ASSERT_TRUE(write);
        auto page = HeapPage::open(*write->page(), address);
        ASSERT_TRUE(page);
        ASSERT_TRUE(page->mark_dead(dead.slot_id()));
    }
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(21, 40);
    const auto context = fetch_context(base::Xid{30}, captured, commit_log);

    EXPECT_FALSE(table.fetch(cold_root, context).value().has_value());
    auto current = table.fetch(cold_successor, context).value();
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(text(current->payload), "new");
    EXPECT_FALSE(table.fetch(dead, context).value().has_value());
    EXPECT_FALSE(
        table.fetch(base::Rid{cold_root.page_id(), base::SlotId{999}}, context).value().has_value()
    );
}

TEST(HeapFetchTest, BrokenOrCrossPageHotMetadataIsCorruption) {
    auto bundle = make_pool();
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{105};
    auto table = HeapTable::create(relation_id, *bundle.pool, registry, heap_log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("old")).value();
    const auto successor = table.insert(base::Xid{21}, 0, bytes("broken")).value();
    install_hot_link(*bundle.pool, relation_id, root, successor, base::Xid{20});
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{21}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(22, 40);
    const auto context = fetch_context(base::Xid{30}, captured, commit_log);

    auto broken = table.fetch(root, context);
    ASSERT_FALSE(broken);
    EXPECT_EQ(broken.error().code(), base::ErrorCode::kCorruption);

    rewrite_header(*bundle.pool, relation_id, root, [](TupleHeader& header) {
        header.ctid = base::Rid{base::PageId{1}, base::SlotId{0}};
    });
    auto cross_page = table.fetch(root, context);
    ASSERT_FALSE(cross_page);
    EXPECT_EQ(cross_page.error().code(), base::ErrorCode::kCorruption);
}

TEST(HeapFetchTest, ConnectedHotCycleTerminatesAsCorruption) {
    auto bundle = make_pool();
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{106};
    auto table = HeapTable::create(relation_id, *bundle.pool, registry, heap_log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("one")).value();
    const auto successor = table.insert(base::Xid{20}, 0, bytes("two")).value();
    install_hot_link(*bundle.pool, relation_id, root, successor, base::Xid{20});
    rewrite_header(*bundle.pool, relation_id, successor, [&](TupleHeader& header) {
        header.xmax = base::Xid{10};
        header.ctid = root;
        header.flags = tuple_flags::kUpdated | tuple_flags::kHotUpdated | tuple_flags::kHeapOnly;
    });
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(21, 40);

    auto fetched = table.fetch(root, fetch_context(base::Xid{30}, captured, commit_log));
    ASSERT_FALSE(fetched);
    EXPECT_EQ(fetched.error().code(), base::ErrorCode::kCorruption);
}

TEST(HeapFetchTest, SerializableRecheckProtectsRejectedCandidateAndInvisibleCreator) {
    auto bundle = make_pool();
    MonotonicHeapLog heap_log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{107};
    auto table = HeapTable::create(relation_id, *bundle.pool, registry, heap_log).value();
    const auto visible = table.insert(base::Xid{5}, 0, bytes("visible but rejected")).value();
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    const auto captured = snapshot(100, 201, {reader, writer});
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, transaction::TransactionStatus::kCommitted);
    transaction::SerializableConflictManager conflicts;
    for (const auto xid : {reader, writer}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, captured));
    }

    auto fetched = table.fetch(visible, fetch_context(reader, captured, commit_log, &conflicts));
    ASSERT_TRUE(fetched);
    ASSERT_TRUE(fetched->has_value());
    // Model a residual index qualification rejecting the owned result. The
    // physical tuple SIREAD was already installed by HeapTable::fetch.
    fetched->reset();
    EXPECT_EQ(conflicts.read_lock_count(reader), 1U);
    ASSERT_TRUE(conflicts.check_write_conflict(
        writer,
        transaction::SsiReadTarget::for_tuple(relation_id, visible)
    ));
    EXPECT_EQ(conflicts.outgoing_conflicts(reader), std::vector<base::Xid>{writer});

    constexpr base::Xid invisible_writer{300};
    const auto invisible = table.insert(invisible_writer, 0, bytes("uncommitted")).value();
    const auto wider_snapshot = snapshot(100, 301, {reader, invisible_writer});
    ASSERT_TRUE(conflicts.register_transaction(invisible_writer));
    ASSERT_TRUE(conflicts.set_snapshot(invisible_writer, wider_snapshot));
    auto invisible_fetch =
        table.fetch(invisible, fetch_context(reader, wider_snapshot, commit_log, &conflicts));
    ASSERT_TRUE(invisible_fetch);
    EXPECT_FALSE(invisible_fetch->has_value());
    EXPECT_EQ(
        conflicts.outgoing_conflicts(reader),
        (std::vector<base::Xid>{writer, invisible_writer})
    );
}

}  // namespace
}  // namespace db::access
