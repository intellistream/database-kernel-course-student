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
#include "db/transaction/backend.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/manager.hpp"
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
                base::Error{base::ErrorCode::kNotFound, "heap delete page not found"}
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
                base::Error{base::ErrorCode::kNotFound, "heap delete relation not found"}
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

    void fail_next_delete() noexcept {
        fail_delete_ = true;
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
        if (fail_delete_ && mutation.operation == HeapWalOperation::kDelete) {
            fail_delete_ = false;
            return base::fail(
                base::Error{base::ErrorCode::kIo, "injected heap delete WAL failure"}
            );
        }
        const auto result = base::Lsn{next_lsn_};
        next_lsn_ += 8U;
        return result;
    }

private:
    std::vector<HeapWalOperation> operations_;
    bool fail_delete_{false};
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

auto mutation_context(
    const base::Xid own_xid,
    const transaction::Snapshot& captured,
    const transaction::CommitLog& commit_log,
    transaction::SerializableConflictManager* const conflicts = nullptr
) noexcept -> HeapMutationContext {
    return HeapMutationContext{
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

auto page_image(
    storage::BufferPool& pool,
    const base::RelationId relation_id,
    const base::PageId page_id
) -> storage::Page::Storage {
    auto read = pool.read_page(storage::PageAddress{relation_id, page_id});
    EXPECT_TRUE(read);
    return read->page()->bytes();
}

auto create_backend(transaction::TransactionManager& manager)
    -> std::unique_ptr<transaction::TransactionBackend> {
    auto backend = transaction::TransactionBackend::create(
        manager,
        transaction::TransactionBackendOptions{.track_persistent_writes = true}
    );
    EXPECT_TRUE(backend);
    return std::move(backend).value();
}

TEST(HeapDeleteTest, CommittedDeleteHidesTheRowButInProgressDeleteDoesNot) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{301};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto rid = table.insert(base::Xid{10}, 0, bytes("row")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    const auto deleting_snapshot = snapshot(11, 21, {base::Xid{20}});
    log.clear();

    auto deleted = table.delete_tuple(
        HeapDeleteRequest{.target = rid},
        mutation_context(base::Xid{20}, deleting_snapshot, commit_log)
    );
    ASSERT_TRUE(deleted);
    const auto* success = std::get_if<HeapDeleteSuccess>(&deleted.value());
    ASSERT_NE(success, nullptr);
    EXPECT_EQ(success->deleted_rid, rid);
    EXPECT_EQ(log.operations(), (std::vector{HeapWalOperation::kDelete}));

    const auto concurrent_snapshot = snapshot(11, 21, {base::Xid{20}});
    auto concurrent =
        table.fetch(rid, fetch_context(base::Xid{30}, concurrent_snapshot, commit_log));
    ASSERT_TRUE(concurrent);
    ASSERT_TRUE(concurrent->has_value());
    EXPECT_EQ(text(concurrent->value().payload), "row");

    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    const auto committed_snapshot = snapshot(21, 31);
    EXPECT_FALSE(table.fetch(rid, fetch_context(base::Xid{30}, committed_snapshot, commit_log))
                     .value()
                     .has_value());
}

TEST(HeapDeleteTest, ExplicitBackendRollbackLogicallyUndoesInsertUpdateAndDelete) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{302};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    transaction::TransactionManager manager;
    auto backend = create_backend(manager);

    auto baseline = backend->start_command();
    ASSERT_TRUE(baseline);
    const auto hot_root =
        table.insert(baseline->xid, baseline->snapshot.command_id, bytes("old")).value();
    const auto cold_root =
        table.insert(baseline->xid, baseline->snapshot.command_id, bytes("cold old")).value();
    const auto deleted_root =
        table.insert(baseline->xid, baseline->snapshot.command_id, bytes("keep")).value();
    ASSERT_TRUE(backend->mark_persistent_write());
    ASSERT_TRUE(backend->finish_command(transaction::CommandOutcome::kSucceeded));
    ASSERT_TRUE(manager.commit_log().is_committed(baseline->xid));

    auto transaction_start = backend->begin_explicit();
    ASSERT_TRUE(transaction_start);
    const auto aborted_xid = transaction_start->xid;
    auto update_command = backend->start_command();
    ASSERT_TRUE(update_command);
    auto hot_update = table.update(
        HeapUpdateRequest{
            .target = hot_root,
            .payload = bytes("new"),
            .indexed_columns_unchanged = true,
        },
        mutation_context(aborted_xid, update_command->snapshot, manager.commit_log())
    );
    ASSERT_TRUE(hot_update);
    const auto hot_version = std::get<HeapUpdateSuccess>(hot_update.value()).rid;
    auto cold_update = table.update(
        HeapUpdateRequest{
            .target = cold_root,
            .payload = bytes("cold new"),
            .indexed_columns_unchanged = false,
        },
        mutation_context(aborted_xid, update_command->snapshot, manager.commit_log())
    );
    ASSERT_TRUE(cold_update);
    const auto cold_success = std::get<HeapUpdateSuccess>(cold_update.value());
    ASSERT_EQ(cold_success.kind, HeapUpdateKind::kCold);
    ASSERT_TRUE(backend->mark_persistent_write());
    ASSERT_TRUE(backend->finish_command(transaction::CommandOutcome::kSucceeded));

    auto second_command = backend->start_command();
    ASSERT_TRUE(second_command);
    const auto aborted_insert =
        table.insert(aborted_xid, second_command->snapshot.command_id, bytes("never")).value();
    auto deleted = table.delete_tuple(
        HeapDeleteRequest{.target = deleted_root},
        mutation_context(aborted_xid, second_command->snapshot, manager.commit_log())
    );
    ASSERT_TRUE(deleted);
    ASSERT_NE(std::get_if<HeapDeleteSuccess>(&deleted.value()), nullptr);
    ASSERT_TRUE(backend->mark_persistent_write());
    ASSERT_TRUE(backend->finish_command(transaction::CommandOutcome::kSucceeded));
    ASSERT_TRUE(backend->rollback_explicit());
    ASSERT_TRUE(manager.commit_log().is_aborted(aborted_xid));

    const auto reader_snapshot =
        snapshot(aborted_xid.value() + 1U, aborted_xid.value() + 10U, {}, 3);
    const auto reader = base::Xid{aborted_xid.value() + 9U};
    auto old = table.fetch(hot_root, fetch_context(reader, reader_snapshot, manager.commit_log()));
    ASSERT_TRUE(old);
    ASSERT_TRUE(old->has_value());
    EXPECT_EQ(text(old->value().payload), "old");
    EXPECT_FALSE(
        table.fetch(hot_version, fetch_context(reader, reader_snapshot, manager.commit_log()))
            .value()
            .has_value()
    );
    auto cold_old =
        table.fetch(cold_root, fetch_context(reader, reader_snapshot, manager.commit_log()));
    ASSERT_TRUE(cold_old);
    ASSERT_TRUE(cold_old->has_value());
    EXPECT_EQ(text(cold_old->value().payload), "cold old");
    EXPECT_FALSE(
        table.fetch(cold_success.rid, fetch_context(reader, reader_snapshot, manager.commit_log()))
            .value()
            .has_value()
    );
    auto kept =
        table.fetch(deleted_root, fetch_context(reader, reader_snapshot, manager.commit_log()));
    ASSERT_TRUE(kept);
    ASSERT_TRUE(kept->has_value());
    EXPECT_EQ(text(kept->value().payload), "keep");
    EXPECT_FALSE(
        table.fetch(aborted_insert, fetch_context(reader, reader_snapshot, manager.commit_log()))
            .value()
            .has_value()
    );
    ASSERT_TRUE(backend->close());
}

TEST(HeapDeleteTest, StaleUpdatedTargetReturnsCurrentCtidWithoutMutation) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{303};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("v1")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    const auto update_snapshot = snapshot(11, 21, {base::Xid{20}});
    auto update = table.update(
        HeapUpdateRequest{
            .target = root,
            .payload = bytes("v2"),
            .indexed_columns_unchanged = true
        },
        mutation_context(base::Xid{20}, update_snapshot, commit_log)
    );
    ASSERT_TRUE(update);
    const auto successor = std::get<HeapUpdateSuccess>(update.value()).rid;
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    const auto delete_snapshot = snapshot(21, 31, {base::Xid{30}});
    log.clear();

    auto stale = table.delete_tuple(
        HeapDeleteRequest{.target = root},
        mutation_context(base::Xid{30}, delete_snapshot, commit_log)
    );

    ASSERT_TRUE(stale);
    const auto* rejected = std::get_if<HeapMutationRejected>(&stale.value());
    ASSERT_NE(rejected, nullptr);
    EXPECT_EQ(rejected->decision.result, transaction::TupleModificationResult::kUpdated);
    EXPECT_EQ(rejected->newer_rid, successor);
    EXPECT_TRUE(log.operations().empty());
}

TEST(HeapDeleteTest, InProgressTupleLockReturnsBlockingWriterWithoutMutation) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{308};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto rid = table.insert(base::Xid{10}, 0, bytes("row")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
    {
        const storage::PageAddress address{relation_id, rid.page_id()};
        auto write = pool->write_page(address);
        ASSERT_TRUE(write);
        auto page = HeapPage::open(*write->page(), address);
        ASSERT_TRUE(page);
        auto tuple = page->mutable_tuple_bytes(rid.slot_id());
        ASSERT_TRUE(tuple);
        auto header = TupleHeader::decode(tuple->first(kTupleHeaderSize)).value();
        header.xmax = base::Xid{20};
        header.ctid = rid;
        header.flags = tuple_flags::kXmaxLockOnly;
        ASSERT_TRUE(header.encode(tuple->first(kTupleHeaderSize)));
    }
    const auto captured = snapshot(11, 31, {base::Xid{20}, base::Xid{30}});
    log.clear();

    auto blocked = table.delete_tuple(
        HeapDeleteRequest{.target = rid},
        mutation_context(base::Xid{30}, captured, commit_log)
    );

    ASSERT_TRUE(blocked);
    const auto* rejected = std::get_if<HeapMutationRejected>(&blocked.value());
    ASSERT_NE(rejected, nullptr);
    EXPECT_EQ(rejected->decision.result, transaction::TupleModificationResult::kBeingModified);
    EXPECT_EQ(rejected->decision.blocking_xid, base::Xid{20});
    EXPECT_FALSE(rejected->newer_rid.has_value());
    EXPECT_TRUE(log.operations().empty());
}

TEST(HeapDeleteTest, ResolvesAPrunedRedirectAndDeletesTheHeapOnlyTarget) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{304};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto root = table.insert(base::Xid{10}, 0, bytes("old")).value();
    const auto target = table.insert(base::Xid{20}, 0, bytes("current")).value();
    {
        const storage::PageAddress address{relation_id, root.page_id()};
        auto write = pool->write_page(address);
        ASSERT_TRUE(write);
        auto page = HeapPage::open(*write->page(), address);
        ASSERT_TRUE(page);
        auto tuple = page->mutable_tuple_bytes(target.slot_id());
        ASSERT_TRUE(tuple);
        auto header = TupleHeader::decode(tuple->first(kTupleHeaderSize)).value();
        header.flags |= tuple_flags::kHeapOnly;
        ASSERT_TRUE(header.encode(tuple->first(kTupleHeaderSize)));
        ASSERT_TRUE(page->redirect(root.slot_id(), target.slot_id()));
    }
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(21, 31, {base::Xid{30}});

    auto deleted = table.delete_tuple(
        HeapDeleteRequest{.target = root},
        mutation_context(base::Xid{30}, captured, commit_log)
    );

    ASSERT_TRUE(deleted);
    const auto* success = std::get_if<HeapDeleteSuccess>(&deleted.value());
    ASSERT_NE(success, nullptr);
    EXPECT_EQ(success->deleted_rid, target);
}

TEST(HeapDeleteTest, SerializableDeleteChecksTupleSireadBeforeMutation) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{305};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto rid = table.insert(base::Xid{5}, 0, bytes("row")).value();
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
            .target = transaction::SsiReadTarget::for_tuple(relation_id, rid),
            .creator_xid = base::Xid{5},
        }
    ));

    auto deleted = table.delete_tuple(
        HeapDeleteRequest{.target = rid},
        mutation_context(writer, captured, commit_log, &conflicts)
    );

    ASSERT_TRUE(deleted);
    EXPECT_NE(std::get_if<HeapDeleteSuccess>(&deleted.value()), nullptr);
    EXPECT_EQ(conflicts.outgoing_conflicts(reader), (std::vector<base::Xid>{writer}));
}

TEST(HeapDeleteTest, DeleteWalFailureRestoresTheExactPageImage) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    const auto relation_id = base::RelationId{306};
    auto table = HeapTable::create(relation_id, *pool, registry, log).value();
    const auto rid = table.insert(base::Xid{10}, 0, bytes("row")).value();
    transaction::CommitLog commit_log;
    set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
    const auto captured = snapshot(11, 21, {base::Xid{20}});
    const auto before = page_image(*pool, relation_id, rid.page_id());
    log.fail_next_delete();

    auto failed = table.delete_tuple(
        HeapDeleteRequest{.target = rid},
        mutation_context(base::Xid{20}, captured, commit_log)
    );

    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code(), base::ErrorCode::kIo);
    EXPECT_EQ(page_image(*pool, relation_id, rid.page_id()), before);
}

TEST(HeapDeleteTest, RejectsInvalidArgumentsBeforePinOrWalMutation) {
    auto pool = make_pool();
    RecordingHeapLog log;
    HeapSpaceRegistry registry;
    auto table = HeapTable::create(base::RelationId{307}, *pool, registry, log).value();
    transaction::CommitLog commit_log;
    const auto captured = snapshot(1, 3);
    log.clear();

    auto invalid = table.delete_tuple(
        HeapDeleteRequest{.target = base::kInvalidRid},
        mutation_context(base::Xid{2}, captured, commit_log)
    );
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code(), base::ErrorCode::kInvalidArgument);

    auto missing_services = table.delete_tuple(
        HeapDeleteRequest{.target = base::Rid{base::PageId{0}, base::SlotId{0}}},
        HeapMutationContext{
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
