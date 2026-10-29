#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "db/access/heap_delete.hpp"
#include "db/access/heap_table.hpp"
#include "db/access/heap_update.hpp"
#include "db/base/error.hpp"
#include "db/base/identifier.hpp"
#include "db/recovery/heap_startup_redo.hpp"
#include "db/recovery/heap_wal.hpp"
#include "db/recovery/wal_durability.hpp"
#include "db/storage/buffer_pool.hpp"
#include "db/storage/disk_manager.hpp"
#include "db/test_support/test_directory.hpp"
#include "db/transaction/commit_log.hpp"
#include "db/transaction/visibility.hpp"
#include "db/wal/manager.hpp"
#include "db/wal/reader.hpp"
#include "db/wal/record.hpp"

namespace db::recovery {
namespace {

constexpr std::uint64_t kIntegrationSystemIdentifier = 0x1020'3040'5060'7080ULL;

auto bytes(const char* const value) -> std::vector<std::byte> {
    const auto length = std::char_traits<char>::length(value);
    const auto view = std::as_bytes(std::span{value, length});
    return {view.begin(), view.end()};
}

void set_status(
    transaction::CommitLog& commit_log,
    const base::Xid xid,
    const transaction::TransactionStatus status
) {
    ASSERT_TRUE(commit_log.ensure_xid_allocated(xid));
    ASSERT_TRUE(commit_log.set_status(xid, status));
}

auto mutation_snapshot(const base::Xid own_xid) -> transaction::Snapshot {
    return transaction::Snapshot{
        .xmin = transaction::kFirstNormalXid,
        .xmax = base::Xid{own_xid.value() + 1U},
        .in_progress = {own_xid},
        .command_id = 1,
    };
}

auto mutation_context(
    const base::Xid own_xid,
    const transaction::Snapshot& snapshot,
    const transaction::CommitLog& commit_log
) noexcept -> access::HeapMutationContext {
    return access::HeapMutationContext{
        .mvcc =
            access::HeapMvccContext{
                .own_xid = own_xid,
                .snapshot = &snapshot,
                .commit_log = &commit_log,
                .serializable_conflicts = nullptr,
            },
        .command_id = snapshot.command_id,
    };
}

auto manager_options(const bool create = true) -> wal::WalManagerOptions {
    return wal::WalManagerOptions{
        .system_identifier = kIntegrationSystemIdentifier,
        .timeline = 1,
        .ring_page_count = 4,
        .retention_lsn = base::kInvalidLsn,
        .redo_lsn = base::kInvalidLsn,
        .create = create,
        .storage_fault_port = nullptr,
    };
}

auto read_records(const std::filesystem::path& wal_directory)
    -> std::vector<wal::DecodedWalRecord> {
    auto reader = wal::WalReader::open(
        wal_directory,
        wal::WalReaderOptions{
            .system_identifier = kIntegrationSystemIdentifier,
            .timeline = 1,
            .retention_lsn = base::kInvalidLsn,
        }
    );
    EXPECT_TRUE(reader);
    std::vector<wal::DecodedWalRecord> records;
    for (;;) {
        auto next = reader->read_next();
        EXPECT_TRUE(next);
        if (!next || !next->has_value()) {
            break;
        }
        records.push_back(std::move(next->value()));
    }
    return records;
}

auto page_key(const storage::PageAddress address) noexcept -> std::uint64_t {
    return (static_cast<std::uint64_t>(address.relation_id.value()) << 32U)
           | address.page_id.value();
}

enum class StartupPortFailure : std::uint8_t {
    kNone,
    kAcquireStandardException,
    kAcquireAllocationException,
    kMarkDirtyStandardException,
};

// Test-only startup page port. It keeps an acquired page resident until the
// test ends, making the page-access contract observable without preempting the
// production bounded cache implementation.
class MemoryStartupPageAccess final : public StartupPageAccessPort {
public:
    explicit MemoryStartupPageAccess(
        const StartupPortFailure failure = StartupPortFailure::kNone
    ) noexcept
        : failure_{failure} {}

    class Lease final : public StartupPageLease {
    public:
        Lease(
            MemoryStartupPageAccess& owner,
            storage::Page& page,
            storage::PageAddress address
        ) noexcept
            : owner_{owner},
              page_{page},
              address_{address} {}

        auto page() noexcept -> storage::Page& override {
            return page_;
        }

        auto mark_dirty() -> base::Status override {
            if (owner_.failure_ == StartupPortFailure::kMarkDirtyStandardException) {
                throw std::runtime_error{"startup page lease failed to mark dirty"};
            }
            if (!dirty_) {
                owner_.dirty_pages_.push_back(address_);
                dirty_ = true;
            }
            return {};
        }

    private:
        MemoryStartupPageAccess& owner_;
        storage::Page& page_;
        storage::PageAddress address_;
        bool dirty_{false};
    };

    auto acquire(const StartupPageRequest request)
        -> base::Result<std::unique_ptr<StartupPageLease>> override {
        if (failure_ == StartupPortFailure::kAcquireStandardException) {
            throw std::runtime_error{"startup page access failed"};
        }
        if (failure_ == StartupPortFailure::kAcquireAllocationException) {
            throw std::bad_alloc{};
        }
        requests_.push_back(request);
        auto found = pages_.find(page_key(request.address));
        if (found == pages_.end() && request.load_mode == StartupPageLoadMode::kReadExisting) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kNotFound,
                    "test startup page access cannot read a missing page",
                }
            );
        }
        if (found == pages_.end()) {
            auto page = storage::Page::create(request.expected_kind, request.address);
            if (!page) {
                return base::fail(std::move(page).error());
            }
            found = pages_.emplace(page_key(request.address), std::move(page).value()).first;
        }
        return std::unique_ptr<StartupPageLease>{
            std::make_unique<Lease>(*this, found->second, request.address),
        };
    }

    auto page(const storage::PageAddress address) const -> const storage::Page& {
        return pages_.at(page_key(address));
    }

    auto requests() const noexcept -> const std::vector<StartupPageRequest>& {
        return requests_;
    }

    auto dirty_pages() const noexcept -> const std::vector<storage::PageAddress>& {
        return dirty_pages_;
    }

private:
    std::map<std::uint64_t, storage::Page> pages_;
    std::vector<StartupPageRequest> requests_;
    std::vector<storage::PageAddress> dirty_pages_;
    StartupPortFailure failure_{StartupPortFailure::kNone};
};

TEST(HeapWalIntegrationTest, ReplaysRealHeapTableMutationsToTheExactFinalPage) {
    auto root = test_support::TestDirectory::create("heap-wal-integration");
    ASSERT_TRUE(root);
    auto manager = wal::WalManager::open(root->join("wal"), manager_options());
    ASSERT_TRUE(manager);
    auto disk = storage::FileDiskManager::open(root->join("relations"));
    ASSERT_TRUE(disk);
    auto pool = storage::BufferPool::create(
        8,
        std::move(disk).value(),
        nullptr,
        std::make_unique<WalPageDurabilityAdapter>(manager.value())
    );
    ASSERT_TRUE(pool);

    constexpr auto relation_id = base::RelationId{501};
    const storage::PageAddress address{relation_id, base::PageId{0}};
    storage::Page::Storage expected_page{};
    HeapMutationWalAdapter adapter{manager.value()};
    {
        access::HeapSpaceRegistry registry;
        auto table = access::HeapTable::create(relation_id, **pool, registry, adapter, 100);
        ASSERT_TRUE(table);
        transaction::CommitLog commit_log;

        const auto hot_root = table->insert(base::Xid{10}, 0, bytes("old"));
        ASSERT_TRUE(hot_root);
        set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
        ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
        const auto update_snapshot = mutation_snapshot(base::Xid{20});
        auto updated = table->update(
            access::HeapUpdateRequest{
                .target = hot_root.value(),
                .payload = bytes("new"),
                .indexed_columns_unchanged = true,
            },
            mutation_context(base::Xid{20}, update_snapshot, commit_log)
        );
        ASSERT_TRUE(updated);
        const auto* hot_updated = std::get_if<access::HeapUpdateSuccess>(&updated.value());
        ASSERT_NE(hot_updated, nullptr);
        set_status(commit_log, base::Xid{20}, transaction::TransactionStatus::kCommitted);

        ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{22}));
        const auto cold_snapshot = mutation_snapshot(base::Xid{22});
        auto cold_updated = table->update(
            access::HeapUpdateRequest{
                .target = hot_updated->rid,
                .payload = bytes("cold"),
                .indexed_columns_unchanged = false,
            },
            mutation_context(base::Xid{22}, cold_snapshot, commit_log)
        );
        ASSERT_TRUE(cold_updated);
        const auto* cold_success = std::get_if<access::HeapUpdateSuccess>(&cold_updated.value());
        ASSERT_NE(cold_success, nullptr);
        EXPECT_EQ(cold_success->kind, access::HeapUpdateKind::kCold);
        set_status(commit_log, base::Xid{22}, transaction::TransactionStatus::kCommitted);

        const auto deleted = table->insert(base::Xid{11}, 0, bytes("delete"));
        ASSERT_TRUE(deleted);
        set_status(commit_log, base::Xid{11}, transaction::TransactionStatus::kCommitted);
        ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{21}));
        const auto delete_snapshot = mutation_snapshot(base::Xid{21});
        ASSERT_TRUE(table->delete_tuple(
            access::HeapDeleteRequest{.target = deleted.value()},
            mutation_context(base::Xid{21}, delete_snapshot, commit_log)
        ));
        set_status(commit_log, base::Xid{21}, transaction::TransactionStatus::kCommitted);

        auto vacuumed = table->vacuum(commit_log, transaction::kXidWrapBoundary);
        ASSERT_TRUE(vacuumed);
        EXPECT_EQ(vacuumed->redirects_installed, 0U);
        EXPECT_EQ(vacuumed->dead_tids.size(), 2U);

        auto read = (*pool)->read_page(address);
        ASSERT_TRUE(read);
        expected_page = read->page()->bytes();
    }

    const auto end_lsn = manager->end_lsn();
    ASSERT_NE(end_lsn, base::kInvalidLsn);
    ASSERT_TRUE((*pool)->shutdown());
    ASSERT_TRUE(manager->flush_through(end_lsn));
    ASSERT_TRUE(manager->shutdown());

    const auto records = read_records(root->join("wal"));
    ASSERT_FALSE(records.empty());
    ASSERT_TRUE(records.front().record.blocks.front().full_page_image.has_value());

    const std::vector<std::uint8_t> expected_operations{
        wal::opcode::heap::kInitialize,
        wal::opcode::heap::kInsert,
        wal::opcode::heap::kHotUpdate,
        wal::opcode::heap::kLock,
        wal::opcode::heap::kInsert,
        wal::opcode::heap::kUpdate,
        wal::opcode::heap::kInsert,
        wal::opcode::heap::kDelete,
        wal::opcode::heap::kPrune,
    };
    std::vector<std::uint8_t> operations;
    operations.reserve(records.size());
    auto replayed = storage::Page::create(storage::PageKind::kHeap, address).value();
    for (const auto& record : records) {
        operations.push_back(record.record.info);
        ASSERT_EQ(apply_heap_redo(record, replayed).value(), HeapRedoDisposition::kApplied);
    }
    EXPECT_EQ(operations, expected_operations);
    EXPECT_EQ(replayed.bytes(), expected_page);

    for (auto record = std::next(records.begin()); record != records.end(); ++record) {
        ASSERT_EQ(apply_heap_redo(*record, replayed).value(), HeapRedoDisposition::kSkipped);
    }
    EXPECT_EQ(replayed.bytes(), expected_page);

    for (std::size_t prefix = 1; prefix <= records.size(); ++prefix) {
        auto crash_page = storage::Page::create(storage::PageKind::kHeap, address).value();
        for (std::size_t index = 0; index < prefix; ++index) {
            ASSERT_EQ(
                apply_heap_redo(records[index], crash_page).value(),
                HeapRedoDisposition::kApplied
            );
        }
        const auto once = crash_page.bytes();
        for (std::size_t index = 0; index < prefix; ++index) {
            ASSERT_TRUE(apply_heap_redo(records[index], crash_page));
        }
        EXPECT_EQ(crash_page.bytes(), once) << "WAL crash prefix " << prefix;
    }

    MemoryStartupPageAccess startup_pages;
    for (const auto& record : records) {
        auto applied = replay_heap_startup_redo(record, startup_pages);
        ASSERT_TRUE(applied) << applied.error().to_string();
        EXPECT_EQ(applied.value(), HeapRedoDisposition::kApplied);
    }
    EXPECT_EQ(startup_pages.page(address).bytes(), expected_page);
    ASSERT_EQ(startup_pages.requests().size(), records.size());
    EXPECT_EQ(startup_pages.requests().front().load_mode, StartupPageLoadMode::kSkipExisting);
    for (std::size_t index = 1U; index < startup_pages.requests().size(); ++index) {
        EXPECT_EQ(startup_pages.requests()[index].load_mode, StartupPageLoadMode::kReadExisting);
    }
    EXPECT_EQ(startup_pages.dirty_pages().size(), records.size());
}

TEST(HeapStartupRedoTest, WillInitializeSkipsAnUntrustedExistingPage) {
    constexpr storage::PageAddress k_address{base::RelationId{505}, base::PageId{0}};
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
            .xid = base::Xid{9},
            .resource_manager = wal::WalResourceManager::kHeap,
            .info = wal::opcode::heap::kInitialize,
            .blocks = {wal::WalBlockReference{
                .relation_id = k_address.relation_id,
                .page_id = k_address.page_id,
                .will_initialize = true,
                .standard_page = true,
                .full_page_image = std::nullopt,
                .delta = std::move(payload).value(),
            }},
            .main_payload = {},
        },
    };

    MemoryStartupPageAccess pages;
    auto applied = replay_heap_startup_redo(record, pages);
    ASSERT_TRUE(applied) << applied.error().to_string();
    EXPECT_EQ(applied.value(), HeapRedoDisposition::kApplied);
    ASSERT_EQ(pages.requests().size(), 1U);
    EXPECT_EQ(pages.requests().front().load_mode, StartupPageLoadMode::kSkipExisting);
    EXPECT_EQ(pages.page(k_address).lsn(), record.end_lsn);
    EXPECT_EQ(pages.dirty_pages(), std::vector<storage::PageAddress>{k_address});
}

TEST(HeapStartupRedoTest, ContainsPagePortExceptions) {
    constexpr storage::PageAddress k_address{base::RelationId{506}, base::PageId{0}};
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
        .start_lsn = base::Lsn{770U},
        .end_lsn = base::Lsn{790U},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{10},
            .resource_manager = wal::WalResourceManager::kHeap,
            .info = wal::opcode::heap::kInitialize,
            .blocks = {wal::WalBlockReference{
                .relation_id = k_address.relation_id,
                .page_id = k_address.page_id,
                .will_initialize = true,
                .standard_page = true,
                .full_page_image = std::nullopt,
                .delta = std::move(payload).value(),
            }},
            .main_payload = {},
        },
    };

    MemoryStartupPageAccess standard_failure{StartupPortFailure::kAcquireStandardException};
    auto internal = replay_heap_startup_redo(record, standard_failure);
    ASSERT_FALSE(internal);
    EXPECT_EQ(internal.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(
        internal.error().message().find("startup page access failed"),
        std::string_view::npos
    );

    MemoryStartupPageAccess allocation_failure{StartupPortFailure::kAcquireAllocationException};
    auto exhausted = replay_heap_startup_redo(record, allocation_failure);
    ASSERT_FALSE(exhausted);
    EXPECT_EQ(exhausted.error().code(), base::ErrorCode::kResourceExhausted);

    MemoryStartupPageAccess dirty_failure{StartupPortFailure::kMarkDirtyStandardException};
    auto dirty = replay_heap_startup_redo(record, dirty_failure);
    ASSERT_FALSE(dirty);
    EXPECT_EQ(dirty.error().code(), base::ErrorCode::kInternal);
    EXPECT_NE(
        dirty.error().message().find("startup page lease failed to mark dirty"),
        std::string_view::npos
    );
    EXPECT_TRUE(dirty_failure.dirty_pages().empty());
}

TEST(HeapStartupRedoTest, RejectsANonHeapRouteBeforePageAcquisition) {
    const wal::DecodedWalRecord record{
        .start_lsn = base::Lsn{800U},
        .end_lsn = base::Lsn{840U},
        .record = wal::WalRecord{
            .previous_lsn = base::kInvalidLsn,
            .xid = base::Xid{11},
            .resource_manager = wal::WalResourceManager::kTransaction,
            .info = wal::opcode::transaction::kCommit,
            .blocks = {},
            .main_payload = {},
        },
    };

    MemoryStartupPageAccess pages;
    auto rejected = replay_heap_startup_redo(record, pages);
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code(), base::ErrorCode::kInvalidArgument);
    EXPECT_TRUE(pages.requests().empty());
}

TEST(HeapWalIntegrationTest, MultiInsertCoalescesNewPageImagesAndReplaysBeyondThePool) {
    auto root = test_support::TestDirectory::create("heap-wal-multi-page");
    ASSERT_TRUE(root);
    auto manager = wal::WalManager::open(root->join("wal"), manager_options());
    ASSERT_TRUE(manager);
    auto disk = storage::FileDiskManager::open(root->join("relations"));
    ASSERT_TRUE(disk);
    constexpr std::size_t k_pool_size = 2;
    auto pool = storage::BufferPool::create(
        k_pool_size,
        std::move(disk).value(),
        nullptr,
        std::make_unique<WalPageDurabilityAdapter>(manager.value())
    );
    ASSERT_TRUE(pool);

    constexpr auto relation_id = base::RelationId{502};
    std::vector<std::vector<std::byte>> rows;
    rows.reserve(32U);
    for (std::size_t value = 0; value < 32U; ++value) {
        rows.emplace_back(700U, static_cast<std::byte>(value));
    }
    std::vector<base::Bytes> row_views;
    row_views.reserve(rows.size());
    for (const auto& row : rows) {
        row_views.emplace_back(row);
    }

    std::vector<storage::Page::Storage> expected_pages;
    {
        HeapMutationWalAdapter adapter{manager.value()};
        access::HeapSpaceRegistry registry;
        auto table = access::HeapTable::create(relation_id, **pool, registry, adapter, 100);
        ASSERT_TRUE(table);
        auto inserted = table->multi_insert(base::Xid{10}, 7, row_views);
        ASSERT_TRUE(inserted);
        EXPECT_EQ(inserted->size(), rows.size());
        ASSERT_GT(table->page_count(), k_pool_size);
        for (const auto page_id : table->page_ids()) {
            auto read = (*pool)->read_page(storage::PageAddress{relation_id, page_id});
            ASSERT_TRUE(read);
            expected_pages.push_back(read->page()->bytes());
        }
    }

    const auto end_lsn = manager->end_lsn();
    ASSERT_TRUE((*pool)->shutdown());
    ASSERT_TRUE(manager->flush_through(end_lsn));
    ASSERT_TRUE(manager->shutdown());
    const auto records = read_records(root->join("wal"));

    std::vector<storage::Page> replayed_pages;
    replayed_pages.reserve(expected_pages.size());
    for (std::size_t page = 0; page < expected_pages.size(); ++page) {
        replayed_pages.push_back(
            storage::Page::create(
                storage::PageKind::kHeap,
                storage::PageAddress{relation_id, base::PageId{static_cast<std::uint32_t>(page)}}
            ).value()
        );
    }
    std::size_t multi_records = 0;
    std::size_t initialized_multi_records = 0;
    std::size_t logged_rows = 0;
    for (const auto& record : records) {
        const auto page = static_cast<std::size_t>(record.record.blocks.front().page_id.value());
        ASSERT_LT(page, replayed_pages.size());
        ASSERT_EQ(
            apply_heap_redo(record, replayed_pages[page]).value(),
            HeapRedoDisposition::kApplied
        );
        if (record.record.info == wal::opcode::heap::kMultiInsert) {
            ++multi_records;
            const auto& block = record.record.blocks.front();
            if (block.will_initialize) {
                ++initialized_multi_records;
                EXPECT_TRUE(block.full_page_image.has_value());
                EXPECT_TRUE(block.delta.empty());
            } else {
                EXPECT_FALSE(block.full_page_image.has_value());
                ASSERT_FALSE(block.delta.empty());
                logged_rows +=
                    decode_heap_wal_payload(record.record.info, block.delta)->tuples.size();
            }
        }
    }
    EXPECT_EQ(multi_records, expected_pages.size());
    EXPECT_EQ(initialized_multi_records, expected_pages.size() - 1U);
    EXPECT_LT(logged_rows, rows.size());
    for (std::size_t page = 0; page < expected_pages.size(); ++page) {
        EXPECT_EQ(replayed_pages[page].bytes(), expected_pages[page]);
    }
}

TEST(HeapWalIntegrationTest, CheckpointUsesOneHeapImageThenASteadyStateDelta) {
    auto root = test_support::TestDirectory::create("heap-wal-checkpoint-policy");
    ASSERT_TRUE(root);
    auto manager = wal::WalManager::open(root->join("wal"), manager_options());
    ASSERT_TRUE(manager);
    auto disk = storage::FileDiskManager::open(root->join("relations"));
    ASSERT_TRUE(disk);
    auto pool = storage::BufferPool::create(
        4,
        std::move(disk).value(),
        nullptr,
        std::make_unique<WalPageDurabilityAdapter>(manager.value())
    );
    ASSERT_TRUE(pool);

    constexpr auto relation_id = base::RelationId{503};
    const storage::PageAddress address{relation_id, base::PageId{0}};
    storage::Page::Storage expected_page{};
    base::Lsn redo_lsn{base::kInvalidLsn};
    {
        HeapMutationWalAdapter adapter{manager.value()};
        access::HeapSpaceRegistry registry;
        auto table = access::HeapTable::create(relation_id, **pool, registry, adapter, 100);
        ASSERT_TRUE(table);
        ASSERT_TRUE(table->insert(base::Xid{10}, 0, bytes("base")));
        auto marker = manager->begin_checkpoint();
        ASSERT_TRUE(marker);
        redo_lsn = marker->start_lsn;
        ASSERT_TRUE(table->insert(base::Xid{20}, 0, bytes("first")));
        ASSERT_TRUE(table->insert(base::Xid{20}, 0, bytes("second")));
        auto read = (*pool)->read_page(address);
        ASSERT_TRUE(read);
        expected_page = read->page()->bytes();
    }

    const auto end_lsn = manager->end_lsn();
    ASSERT_TRUE((*pool)->shutdown());
    ASSERT_TRUE(manager->flush_through(end_lsn));
    ASSERT_TRUE(manager->shutdown());
    const auto records = read_records(root->join("wal"));
    std::vector<wal::DecodedWalRecord> after_checkpoint;
    for (const auto& record : records) {
        if (record.start_lsn.value() > redo_lsn.value()
            && record.record.resource_manager == wal::WalResourceManager::kHeap) {
            after_checkpoint.push_back(record);
        }
    }
    ASSERT_EQ(after_checkpoint.size(), 2U);
    EXPECT_TRUE(after_checkpoint[0].record.blocks.front().full_page_image.has_value());
    EXPECT_TRUE(after_checkpoint[0].record.blocks.front().delta.empty());
    EXPECT_FALSE(after_checkpoint[1].record.blocks.front().full_page_image.has_value());
    EXPECT_FALSE(after_checkpoint[1].record.blocks.front().delta.empty());

    auto replayed = storage::Page::create(storage::PageKind::kHeap, address).value();
    ASSERT_EQ(
        apply_heap_redo(after_checkpoint[0], replayed).value(),
        HeapRedoDisposition::kApplied
    );
    ASSERT_EQ(
        apply_heap_redo(after_checkpoint[1], replayed).value(),
        HeapRedoDisposition::kApplied
    );
    EXPECT_EQ(replayed.bytes(), expected_page);
}

TEST(HeapWalIntegrationTest, ReopensFlushedHeapAndContinuesTheWalStream) {
    auto root = test_support::TestDirectory::create("heap-wal-reopen");
    ASSERT_TRUE(root);
    constexpr auto relation_id = base::RelationId{504};
    base::Rid stable_root{base::kInvalidRid};
    base::Rid untouched{base::kInvalidRid};
    base::Lsn durable_end{base::kInvalidLsn};

    {
        auto manager = wal::WalManager::open(root->join("wal"), manager_options());
        ASSERT_TRUE(manager);
        auto disk = storage::FileDiskManager::open(root->join("relations"));
        ASSERT_TRUE(disk);
        auto pool = storage::BufferPool::create(
            2,
            std::move(disk).value(),
            nullptr,
            std::make_unique<WalPageDurabilityAdapter>(manager.value())
        );
        ASSERT_TRUE(pool);
        HeapMutationWalAdapter adapter{manager.value()};
        access::HeapSpaceRegistry registry;
        auto table = access::HeapTable::create(relation_id, **pool, registry, adapter, 100);
        ASSERT_TRUE(table);
        transaction::CommitLog commit_log;
        stable_root = table->insert(base::Xid{10}, 0, bytes("old")).value();
        untouched = table->insert(base::Xid{11}, 0, bytes("kept")).value();
        set_status(commit_log, base::Xid{10}, transaction::TransactionStatus::kCommitted);
        set_status(commit_log, base::Xid{11}, transaction::TransactionStatus::kCommitted);
        ASSERT_TRUE(commit_log.ensure_xid_allocated(base::Xid{20}));
        const auto captured = mutation_snapshot(base::Xid{20});
        ASSERT_TRUE(table->update(
            access::HeapUpdateRequest{
                .target = stable_root,
                .payload = bytes("new"),
                .indexed_columns_unchanged = true,
            },
            mutation_context(base::Xid{20}, captured, commit_log)
        ));
        durable_end = manager->end_lsn();
        ASSERT_TRUE((*pool)->shutdown());
        ASSERT_TRUE(manager->flush_through(durable_end));
        ASSERT_TRUE(manager->shutdown());
    }

    auto manager = wal::WalManager::open(root->join("wal"), manager_options(false));
    ASSERT_TRUE(manager);
    EXPECT_EQ(manager->end_lsn(), durable_end);
    auto disk = storage::FileDiskManager::open(root->join("relations"));
    ASSERT_TRUE(disk);
    auto pool = storage::BufferPool::create(
        2,
        std::move(disk).value(),
        nullptr,
        std::make_unique<WalPageDurabilityAdapter>(manager.value())
    );
    ASSERT_TRUE(pool);
    HeapMutationWalAdapter adapter{manager.value()};
    access::HeapSpaceRegistry registry;
    auto table = access::HeapTable::open(relation_id, **pool, registry, adapter, 100);
    ASSERT_TRUE(table);
    transaction::CommitLog commit_log;
    for (const auto xid : {base::Xid{10}, base::Xid{11}, base::Xid{20}}) {
        set_status(commit_log, xid, transaction::TransactionStatus::kCommitted);
    }
    const transaction::Snapshot snapshot{
        .xmin = transaction::kFirstNormalXid,
        .xmax = base::Xid{40},
        .in_progress = {base::Xid{30}},
        .command_id = 1,
    };
    const access::HeapFetchContext context{
        .mvcc =
            access::HeapMvccContext{
                .own_xid = base::Xid{30},
                .snapshot = &snapshot,
                .commit_log = &commit_log,
                .serializable_conflicts = nullptr,
            },
        .prune_xmin = std::nullopt,
    };
    auto updated = table->fetch(stable_root, context);
    auto kept = table->fetch(untouched, context);
    ASSERT_TRUE(updated);
    ASSERT_TRUE(updated->has_value());
    EXPECT_EQ(updated->value().payload, bytes("new"));
    ASSERT_TRUE(kept);
    ASSERT_TRUE(kept->has_value());
    EXPECT_EQ(kept->value().payload, bytes("kept"));
    ASSERT_TRUE(table->insert(base::Xid{30}, 0, bytes("after reopen")));
    EXPECT_GT(manager->end_lsn().value(), durable_end.value());
    const auto reopened_end = manager->end_lsn();
    ASSERT_TRUE((*pool)->shutdown());
    ASSERT_TRUE(manager->flush_through(reopened_end));
    ASSERT_TRUE(manager->shutdown());
}

}  // namespace
}  // namespace db::recovery
