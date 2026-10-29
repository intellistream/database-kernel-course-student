#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/heap_page.hpp"
#include "db/access/heap_table.hpp"
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
            return base::fail(
                base::Error{base::ErrorCode::kNotFound, "heap update page not found"}
            );
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
                base::Error{base::ErrorCode::kNotFound, "heap update relation not found"}
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

class RecordingHeapLog final : public HeapMutationLogPort {
public:
    void clear() {
        operations_.clear();
    }

    void fail_next(const HeapWalOperation operation) {
        failure_ = operation;
    }

    auto operations() const noexcept -> const std::vector<HeapWalOperation>& {
        return operations_;
    }

    auto log(const HeapWalMutation& mutation) -> base::Result<base::Lsn> override {
        if (mutation.after_page == nullptr) {
            return base::fail(
                base::Error{base::ErrorCode::kInvalidArgument, "missing heap after-image"}
            );
        }
        operations_.push_back(mutation.operation);
        if (failure_.has_value() && failure_.value() == mutation.operation) {
            failure_.reset();
            return base::fail(
                base::Error{base::ErrorCode::kIo, "injected heap update WAL failure"}
            );
        }
        const auto result = base::Lsn{next_lsn_};
        next_lsn_ += 8U;
        return result;
    }

private:
    std::vector<HeapWalOperation> operations_;
    std::optional<HeapWalOperation> failure_;
    std::uint64_t next_lsn_{8};
};

auto make_pool() -> std::unique_ptr<storage::BufferPool> {
    auto pool = storage::BufferPool::create(
        8,
        std::make_unique<MemoryDisk>(),
        std::move(storage::LruReplacer::create(8)).value(),
        std::make_unique<AcceptDurability>()
    );
    EXPECT_TRUE(pool) << pool.error().to_string();
    return std::move(pool).value();
}

auto bytes(const std::string& value) -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.reserve(value.size());
    for (const auto character : value) {
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

auto update_context(
    const base::Xid own_xid,
    const transaction::Snapshot& captured,
    const transaction::CommitLog& commit_log,
    transaction::SerializableConflictManager* const conflicts = nullptr
) noexcept -> HeapUpdateContext {
    return HeapUpdateContext{
        .mvcc =
            HeapMvccContext{
                .own_xid = own_xid,
                .snapshot = &captured,
                .commit_log = &commit_log,
                .serializable_conflicts = conflicts,
            },
        .command_id = captured.command_id,
    };
}

auto fetch_context(
    const base::Xid own_xid,
    const transaction::Snapshot& captured,
    const transaction::CommitLog& commit_log
) noexcept -> HeapFetchContext {
    return HeapFetchContext{
        .mvcc = HeapMvccContext{
            .own_xid = own_xid,
            .snapshot = &captured,
            .commit_log = &commit_log,
        },
    };
}

auto read_header(storage::BufferPool& pool, const base::RelationId relation_id, const base::Rid rid)
    -> TupleHeader {
    const storage::PageAddress address{relation_id, rid.page_id()};
    auto read = pool.read_page(address);
    EXPECT_TRUE(read);
    auto copy = *read->page();
    auto page = HeapPage::open(copy, address);
    EXPECT_TRUE(page);
    auto tuple = page->tuple(rid.slot_id());
    EXPECT_TRUE(tuple);
    return tuple->header;
}

auto page_image(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const base::PageId page_id
) -> storage::Page::Storage {
    auto read = pool.read_page(storage::PageAddress{relation_id, page_id});
    EXPECT_TRUE(read);
    return read->page()->bytes();
}

TEST(HeapUpdateTest, SamePageHotUpdateBuildsAValidChainWithoutIndexMaintenance) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{201};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("old")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    const auto writer_snapshot = snapshot(11, 21, {base::Xid{20}});
    log.clear();

    auto updated = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("new"),
            .indexed_columns_unchanged = true,
        },
        update_context(base::Xid{20}, writer_snapshot, commit_log)
    );

    ASSERT_TRUE(updated);
    const auto* success = std::get_if<HeapUpdateSuccess>(&updated.value());
    ASSERT_NE(success, nullptr);
    EXPECT_EQ(success->kind, HeapUpdateKind::kHot);
    EXPECT_FALSE(success->index_maintenance_required());
    EXPECT_EQ(success->rid.page_id(), root.page_id());
    EXPECT_EQ(log.operations(), (std::vector{HeapWalOperation::kHotUpdate}));

    const auto old_header = read_header(*pool, relation_id, root);
    const auto new_header = read_header(*pool, relation_id, success->rid);
    EXPECT_EQ(old_header.xmax, base::Xid{20});
    EXPECT_EQ(old_header.ctid, success->rid);
    EXPECT_TRUE(old_header.has_flag(tuple_flags::kUpdated));
    EXPECT_TRUE(old_header.has_flag(tuple_flags::kHotUpdated));
    EXPECT_FALSE(old_header.has_flag(tuple_flags::kXmaxLockOnly));
    EXPECT_EQ(new_header.xmin, base::Xid{20});
    EXPECT_EQ(new_header.ctid, success->rid);
    EXPECT_TRUE(new_header.has_flag(tuple_flags::kHeapOnly));
}

TEST(HeapUpdateTest, IndexedColumnChangeForcesColdUpdateAndIndexMaintenance) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{202};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("old")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    const auto writer_snapshot = snapshot(11, 21, {base::Xid{20}});
    log.clear();

    auto updated = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("new indexed key"),
            .indexed_columns_unchanged = false,
        },
        update_context(base::Xid{20}, writer_snapshot, commit_log)
    );

    ASSERT_TRUE(updated);
    const auto* success = std::get_if<HeapUpdateSuccess>(&updated.value());
    ASSERT_NE(success, nullptr);
    EXPECT_EQ(success->kind, HeapUpdateKind::kCold);
    EXPECT_TRUE(success->index_maintenance_required());
    EXPECT_EQ(
        log.operations(),
        (std::vector{
            HeapWalOperation::kLock,
            HeapWalOperation::kInsert,
            HeapWalOperation::kUpdate,
        })
    );
    const auto old_header = read_header(*pool, relation_id, root);
    const auto new_header = read_header(*pool, relation_id, success->rid);
    EXPECT_TRUE(old_header.has_flag(tuple_flags::kUpdated));
    EXPECT_FALSE(old_header.has_flag(tuple_flags::kHotUpdated));
    EXPECT_FALSE(old_header.has_flag(tuple_flags::kXmaxLockOnly));
    EXPECT_EQ(old_header.ctid, success->rid);
    EXPECT_FALSE(new_header.has_flag(tuple_flags::kHeapOnly));
}

TEST(HeapUpdateTest, SuccessiveHotUpdatesProduceAFetchableConnectedChain) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{209};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("v1")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    const auto first_snapshot = snapshot(11, 21, {base::Xid{20}});
    auto first = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("v2"),
            .indexed_columns_unchanged = true
        },
        update_context(base::Xid{20}, first_snapshot, commit_log)
    );
    ASSERT_TRUE(first);
    const auto middle = std::get<HeapUpdateSuccess>(first.value()).rid;
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);

    const auto second_snapshot = snapshot(21, 31, {base::Xid{30}}, 2);
    auto second = table.update(
        HeapUpdateRequest{
            .target = middle,
            .payload = bytes("v3"),
            .indexed_columns_unchanged = true,
        },
        update_context(base::Xid{30}, second_snapshot, commit_log)
    );
    ASSERT_TRUE(second);
    const auto final = std::get<HeapUpdateSuccess>(second.value()).rid;
    set_status(commit_log, base::Xid{30}, transaction::TransactionStatus::kCommitted);

    const auto middle_header = read_header(*pool, relation_id, middle);
    EXPECT_TRUE(middle_header.has_flag(tuple_flags::kHeapOnly));
    EXPECT_TRUE(middle_header.has_flag(tuple_flags::kUpdated));
    EXPECT_TRUE(middle_header.has_flag(tuple_flags::kHotUpdated));
    EXPECT_EQ(middle_header.xmax, base::Xid{30});
    EXPECT_EQ(middle_header.ctid, final);

    const auto reader_snapshot = snapshot(31, 41, {}, 3);
    auto fetched = table.fetch(root, fetch_context(base::Xid{40}, reader_snapshot, commit_log));
    ASSERT_TRUE(fetched);
    ASSERT_TRUE(fetched->has_value());
    EXPECT_EQ(fetched->value().rid, final);
    EXPECT_EQ(text(fetched->value().payload), "v3");
}

TEST(HeapUpdateTest, HotSpaceFailureFallsBackToCrossPageColdUpdate) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{203};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const std::vector<std::byte> large(3000, std::byte{0xaa});
    const std::vector<std::byte> replacement(3000, std::byte{0xbb});
    const auto root = table.insert(base::Xid{10}, 0, large).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    const auto writer_snapshot = snapshot(11, 21, {base::Xid{20}});
    log.clear();

    auto updated = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = replacement,
            .indexed_columns_unchanged = true,
        },
        update_context(base::Xid{20}, writer_snapshot, commit_log)
    );

    ASSERT_TRUE(updated);
    const auto* success = std::get_if<HeapUpdateSuccess>(&updated.value());
    ASSERT_NE(success, nullptr);
    EXPECT_EQ(success->kind, HeapUpdateKind::kCold);
    EXPECT_NE(success->rid.page_id(), root.page_id());
    EXPECT_TRUE(success->index_maintenance_required());
    EXPECT_EQ(log.operations().front(), HeapWalOperation::kLock);
    EXPECT_EQ(log.operations().back(), HeapWalOperation::kUpdate);
    EXPECT_NE(
        std::find(log.operations().begin(), log.operations().end(), HeapWalOperation::kInsert),
        log.operations().end()
    );
    auto page = pool->read_page(storage::PageAddress{relation_id, root.page_id()});
    ASSERT_TRUE(page);
    ASSERT_TRUE(page->page()->flags());
    EXPECT_NE(page->page()->flags().value() & heap_page_flags::kFull, 0U);
}

TEST(HeapUpdateTest, StaleCommittedRootReturnsUpdatedDecisionAndSuccessor) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{204};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("v1")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    const auto first_snapshot = snapshot(11, 21, {base::Xid{20}});
    auto first = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("v2"),
            .indexed_columns_unchanged = true
        },
        update_context(base::Xid{20}, first_snapshot, commit_log)
    );
    ASSERT_TRUE(first);
    const auto successor = std::get<HeapUpdateSuccess>(first.value()).rid;
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    const auto second_snapshot = snapshot(21, 31, {base::Xid{30}});
    log.clear();

    auto stale = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("v3"),
            .indexed_columns_unchanged = true
        },
        update_context(base::Xid{30}, second_snapshot, commit_log)
    );

    ASSERT_TRUE(stale);
    const auto* rejected = std::get_if<HeapUpdateRejected>(&stale.value());
    ASSERT_NE(rejected, nullptr);
    EXPECT_EQ(rejected->decision.result, transaction::TupleModificationResult::kUpdated);
    EXPECT_EQ(rejected->newer_rid, successor);
    EXPECT_TRUE(log.operations().empty());
}

TEST(HeapUpdateTest, InProgressTupleLockReturnsBlockingWriterWithoutMutation) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{205};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("row")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    {
        const storage::PageAddress address{relation_id, root.page_id()};
        auto write = pool->write_page(address);
        ASSERT_TRUE(write);
        auto page = HeapPage::open(*write->page(), address);
        ASSERT_TRUE(page);
        auto tuple = page->mutable_tuple_bytes(root.slot_id());
        ASSERT_TRUE(tuple);
        auto header = TupleHeader::decode(tuple->first(kTupleHeaderSize)).value();
        header.xmax = base::Xid{20};
        header.ctid = root;
        header.flags = tuple_flags::kXmaxLockOnly;
        ASSERT_TRUE(header.encode(tuple->first(kTupleHeaderSize)));
    }
    const auto captured = snapshot(11, 31, {base::Xid{20}, base::Xid{30}});
    log.clear();

    auto blocked = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("new"),
            .indexed_columns_unchanged = true
        },
        update_context(base::Xid{30}, captured, commit_log)
    );

    ASSERT_TRUE(blocked);
    const auto* rejected = std::get_if<HeapUpdateRejected>(&blocked.value());
    ASSERT_NE(rejected, nullptr);
    EXPECT_EQ(rejected->decision.result, transaction::TupleModificationResult::kBeingModified);
    EXPECT_EQ(rejected->decision.blocking_xid, base::Xid{20});
    EXPECT_FALSE(rejected->newer_rid.has_value());
    EXPECT_TRUE(log.operations().empty());
}

TEST(HeapUpdateTest, SerializableWriteChecksExistingTupleSireadBeforeMutation) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{206};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto root = table.insert(base::Xid{5}, 0, bytes("row")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{5}, transaction::TransactionStatus::kCommitted);
    constexpr base::Xid reader{100};
    constexpr base::Xid writer{200};
    const auto captured = snapshot(100, 201, {reader, writer});
    transaction::SerializableConflictManager conflicts;
    for (const auto xid : {reader, writer}) {
        ASSERT_TRUE(conflicts.register_transaction(xid));
        ASSERT_TRUE(conflicts.set_snapshot(xid, captured));
    }
    ASSERT_TRUE(conflicts.record_read(
        reader,
        transaction::SsiReadRecord{
            .target = transaction::SsiReadTarget::for_tuple(relation_id, root),
            .creator_xid = base::Xid{5},
        }
    ));

    auto updated = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("changed"),
            .indexed_columns_unchanged = true,
        },
        update_context(writer, captured, commit_log, &conflicts)
    );

    ASSERT_TRUE(updated);
    EXPECT_NE(std::get_if<HeapUpdateSuccess>(&updated.value()), nullptr);
    EXPECT_EQ(conflicts.outgoing_conflicts(reader), (std::vector<base::Xid>{writer}));
}

TEST(HeapUpdateTest, HotWalFailureRestoresTheExactPageImage) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{207};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("before")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(11, 21, {base::Xid{20}});
    const auto before = page_image(*pool, relation_id, root.page_id());
    log.fail_next(HeapWalOperation::kHotUpdate);

    auto failed = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("after"),
            .indexed_columns_unchanged = true,
        },
        update_context(base::Xid{20}, captured, commit_log)
    );

    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(page_image(*pool, relation_id, root.page_id()), before);
}

TEST(HeapUpdateTest, RejectsInvalidArgumentsBeforePinOrWalMutation) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{208}, *pool, registry, log).value();
    transaction::CommitLog commit_log;
    const auto captured = snapshot(1, 3);
    log.clear();

    auto invalid = table.update(
        HeapUpdateRequest{.target = base::kInvalidRid, .payload = bytes("row")},
        update_context(base::Xid{2}, captured, commit_log)
    );
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);

    auto missing_services = table.update(
        HeapUpdateRequest{
            .target = base::Rid{base::PageId{0}, base::SlotId{0}},
            .payload = bytes("row"),
        },
        HeapUpdateContext{
            .mvcc = HeapMvccContext{.own_xid = base::Xid{2}},
            .command_id = 1,
        }
    );
    ASSERT_FALSE(missing_services);
    EXPECT_EQ(missing_services.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_TRUE(log.operations().empty());
}

}  // namespace
}  // namespace db::access
